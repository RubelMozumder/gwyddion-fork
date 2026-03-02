/*
 *  $Id: gwystock.c 25436 2023-06-14 15:32:55Z yeti-dn $
 *  Copyright (C) 2003 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkiconfactory.h>
#include <gdk/gdkkeysyms.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include "gwystock.h"

static void           register_icons           (const gchar **pixmap_paths,
                                                GtkIconFactory *icon_factory);
static void           slurp_icon_directory     (const gchar *path,
                                                GHashTable *icons);
static void           register_icon_set_list_cb(const gchar *id,
                                                GList *list,
                                                GtkIconFactory *factory);
static GtkIconSource* file_to_icon_source      (const gchar *path,
                                                const gchar *filename,
                                                gchar **id);

static GtkIconFactory *the_icon_factory = NULL;

/**
 * gwy_stock_register_stock_items:
 *
 * Registers stock items.
 *
 * This function must be called before any stock items are used.
 **/
void
gwy_stock_register_stock_items(void)
{
    gchar *pixmap_paths[3];

    g_return_if_fail(!the_icon_factory);
    gtk_icon_size_register(GWY_ICON_SIZE_ABOUT, 60, 60);
    pixmap_paths[0] = gwy_find_self_dir("pixmaps");
    pixmap_paths[1] = g_build_filename(gwy_get_user_dir(), "pixmaps", NULL);
    if (!g_file_test(pixmap_paths[1], G_FILE_TEST_IS_DIR)) {
        g_free(pixmap_paths[1]);
        pixmap_paths[1] = NULL;
    }
    pixmap_paths[2] = NULL;

    the_icon_factory = gtk_icon_factory_new();
    register_icons((const gchar**)pixmap_paths, the_icon_factory);

    g_free(pixmap_paths[0]);
    g_free(pixmap_paths[1]);
    gtk_icon_factory_add_default(the_icon_factory);
}

static void
register_icons(const gchar **pixmap_paths,
               GtkIconFactory *icon_factory)
{
    GHashTable *icons;

    icons = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    while (*pixmap_paths) {
        slurp_icon_directory(*pixmap_paths, icons);
        pixmap_paths++;
    }
    g_hash_table_foreach(icons, (GHFunc)register_icon_set_list_cb,
                         icon_factory);
    g_hash_table_destroy(icons);
}

/* XXX: not only registers the icons but also frees the list */
static void
register_icon_set_list_cb(const gchar *id,
                          GList *list,
                          GtkIconFactory *factory)
{
    GtkIconSet *icon_set;
    GtkIconSource *icon_source, *largest;
    GtkIconSize gtksize;
    GList *l;
    gint max, w, h;

    icon_set = gtk_icon_set_new();
    max = 0;
    largest = NULL;
    for (l = list; l; l = g_list_next(l)) {
        icon_source = (GtkIconSource*)l->data;
        gtksize = gtk_icon_source_get_size(icon_source);
        g_assert(gtk_icon_size_lookup(gtksize, &w, &h));
        if (w*h > max)
            largest = icon_source;
    }
    if (!largest) {
        g_warning("No icon of nonzero size in the set");
        return;
    }
    gtk_icon_source_set_size_wildcarded(largest, TRUE);

    for (l = list; l; l = g_list_next(l)) {
        icon_source = (GtkIconSource*)l->data;
        gtk_icon_set_add_source(icon_set, icon_source);
        gtk_icon_source_free(icon_source);
    }
    gtk_icon_factory_add(factory, id, icon_set);
    g_list_free(list);
}

static void
slurp_icon_directory(const gchar *path,
                     GHashTable *icons)
{
    GtkIconSource *icon_source;
    GDir *gdir = NULL;
    GError *err = NULL;
    const gchar *filename;
    GList *list;
    gchar *id;

    gdir = g_dir_open(path, 0, &err);
    if (!gdir) {
        g_warning("Cannot open directory `%s': %s", path, err->message);
        return;
    }

    while ((filename = g_dir_read_name(gdir))) {
        icon_source = file_to_icon_source(path, filename, &id);
        if (!icon_source) {
            g_free(id);
            id = NULL;
            continue;
        }
        list = (GList*)g_hash_table_lookup(icons, id);
        list = g_list_append(list, icon_source);
        g_hash_table_replace(icons, id, list);
    }
    g_dir_close(gdir);
}

/*
 * Filename format: <gwy_foobar>-<size>[.<state>].png
 */
static GtkIconSource*
file_to_icon_source(const gchar *path,
                    const gchar *filename,
                    gchar **id)
{
    static struct { gchar letter; GtkStateType state; }
    const state_letters[] = {
        { 'n', GTK_STATE_NORMAL },
        { 'a', GTK_STATE_ACTIVE },
        { 'p', GTK_STATE_PRELIGHT },
        { 's', GTK_STATE_SELECTED },
        { 'i', GTK_STATE_INSENSITIVE },
    };
    /* FIXME: Of course, this is conceptually wrong.  however some guess is
     * better than nothing when we have more than one size of the same icon */
    static struct { gint size; GtkIconSize gtksize; }
    gtk_sizes[] = {
        { 16, GTK_ICON_SIZE_MENU },
        { 18, GTK_ICON_SIZE_SMALL_TOOLBAR },
        { 20, GTK_ICON_SIZE_BUTTON },
        { 24, GTK_ICON_SIZE_LARGE_TOOLBAR },
        { 32, GTK_ICON_SIZE_DND },
        { 48, GTK_ICON_SIZE_DIALOG },
        { 60, 0 },
    };
    GtkIconSource *icon_source;
    GtkIconSize gtksize = -1;
    GtkStateType state = -1;
    gchar *sz, *st, *p, *fullpath;
    gint size;
    gsize i;

    if (!gtk_sizes[G_N_ELEMENTS(gtk_sizes)-1].gtksize) {
        gtk_sizes[G_N_ELEMENTS(gtk_sizes)-1].gtksize
            = gtk_icon_size_from_name(GWY_ICON_SIZE_ABOUT);
    }
    *id = g_strdup(filename);
    if (!g_str_has_suffix(filename, ".png"))
        return NULL;
    if (!(sz = strchr(*id, '-')))
        return NULL;
    *sz = '\0';
    sz++;
    if (!(st = strchr(sz, '.')))
        return NULL;
    *st = '\0';
    st++;
    if ((p = strchr(st, '.')))
        *p = '\0';
    else
        st = NULL;
    size = atoi(sz);
    if (size < 0)
        return NULL;

    if (st) {
        if (strlen(st) != 1)
            return NULL;
        for (i = 0; i < G_N_ELEMENTS(state_letters); i++) {
            if (st[0] == state_letters[i].letter) {
                state = state_letters[i].state;
                break;
            }
        }
    }

    for (i = 0; i < G_N_ELEMENTS(gtk_sizes); i++) {
        if (gtk_sizes[i].size == size) {
            gtksize = gtk_sizes[i].gtksize;
            break;
        }
        if (gtk_sizes[i].size > size) {
            if (!i)
                gtksize = gtk_sizes[i].gtksize;
            else if (size*size > gtk_sizes[i-1].size*gtk_sizes[i].size)
                gtksize = gtk_sizes[i].gtksize;
            else
                gtksize = gtk_sizes[i-1].gtksize;
            break;
        }
    }
    if (gtksize == (GtkIconSize)-1)
        gtksize = gtk_sizes[G_N_ELEMENTS(gtk_sizes)-1].gtksize;

    icon_source = gtk_icon_source_new();
    fullpath = g_build_filename(path, filename, NULL);
    gtk_icon_source_set_filename(icon_source, fullpath);
    g_free(fullpath);
    gtk_icon_source_set_size(icon_source, gtksize);
    gtk_icon_source_set_direction_wildcarded(icon_source, TRUE);
    gtk_icon_source_set_size_wildcarded(icon_source, FALSE);
    if (state != (GtkStateType)-1) {
        gtk_icon_source_set_state_wildcarded(icon_source, FALSE);
        gtk_icon_source_set_state(icon_source, state);
    }

    return icon_source;
}

/************************** Documentation ****************************/

/**
 * SECTION:gwystock
 * @title: gwystock
 * @short_description: Stock icons
 *
 * Use gwy_stock_register_stock_items() to register stock icons.
 **/

/**
 * GWY_ICON_SIZE_ABOUT:
 *
 * The icon size name for about dialog icon size.
 *
 * Note: This is the name, you have to use gtk_icon_size_from_name() to get a
 * #GtkIconSize from it.
 **/

/* The following generated part is updated by running utils/stockgen.py */

/* @@@ GENERATED STOCK LIST BEGIN @@@ */
/**
 * GWY_STOCK_3D_BASE:
 *
 * The "3D-Base" stock icon.
 * <inlinegraphic fileref="gwy_3d_base-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_ARITHMETIC:
 *
 * The "Arithmetic" stock icon.
 * <inlinegraphic fileref="gwy_arithmetic-24.png" format="PNG"/>
 *
 * Since: 2.3
 **/

/**
 * GWY_STOCK_BINNING:
 *
 * The "Binning" stock icon.
 * <inlinegraphic fileref="gwy_binning-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_BOLD:
 *
 * The "Bold" stock icon.
 * <inlinegraphic fileref="gwy_bold-20.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_CANTILEVER:
 *
 * The "Cantilever" stock icon.
 * <inlinegraphic fileref="gwy_cantilever-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_COLOR_RANGE:
 *
 * The "Color-Range" stock icon.
 * <inlinegraphic fileref="gwy_color_range-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_COLOR_RANGE_ADAPTIVE:
 *
 * The "Color-Range-Adaptive" stock icon.
 * <inlinegraphic fileref="gwy_color_range_adaptive-24.png" format="PNG"/>
 *
 * Since: 2.7
 **/

/**
 * GWY_STOCK_COLOR_RANGE_AUTO:
 *
 * The "Color-Range-Auto" stock icon.
 * <inlinegraphic fileref="gwy_color_range_auto-24.png" format="PNG"/>
 *
 * Since: 2.7
 **/

/**
 * GWY_STOCK_COLOR_RANGE_FIXED:
 *
 * The "Color-Range-Fixed" stock icon.
 * <inlinegraphic fileref="gwy_color_range_fixed-24.png" format="PNG"/>
 *
 * Since: 2.7
 **/

/**
 * GWY_STOCK_COLOR_RANGE_FULL:
 *
 * The "Color-Range-Full" stock icon.
 * <inlinegraphic fileref="gwy_color_range_full-24.png" format="PNG"/>
 *
 * Since: 2.7
 **/

/**
 * GWY_STOCK_CONVOLUTION:
 *
 * The "Convolution" stock icon.
 * <inlinegraphic fileref="gwy_convolution-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_CONVOLVE:
 *
 * The "Convolve" stock icon.
 * <inlinegraphic fileref="gwy_convolve-24.png" format="PNG"/>
 *
 * Since: 2.52
 **/

/**
 * GWY_STOCK_CORRECT_AFFINE:
 *
 * The "Correct-Affine" stock icon.
 * <inlinegraphic fileref="gwy_correct_affine-24.png" format="PNG"/>
 *
 * Since: 2.37
 **/

/**
 * GWY_STOCK_CORRELATION_LENGTH:
 *
 * The "Correlation-Length" stock icon.
 * <inlinegraphic fileref="gwy_correlation_length-24.png" format="PNG"/>
 *
 * Since: 2.56
 **/

/**
 * GWY_STOCK_CORRELATION_MASK:
 *
 * The "Correlation-Mask" stock icon.
 * <inlinegraphic fileref="gwy_correlation_mask-24.png" format="PNG"/>
 *
 * Since: 2.48
 **/

/**
 * GWY_STOCK_CROP:
 *
 * The "Crop" stock icon.
 * <inlinegraphic fileref="gwy_crop-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_CROSS_PROFILE:
 *
 * The "Cross-Profile" stock icon.
 * <inlinegraphic fileref="gwy_cross_profile-24.png" format="PNG"/>
 *
 * Since: 2.53
 **/

/**
 * GWY_STOCK_CURVATURE:
 *
 * The "Curvature" stock icon.
 * <inlinegraphic fileref="gwy_curvature-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_CWT:
 *
 * The "CWT" stock icon.
 * <inlinegraphic fileref="gwy_cwt-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_DATA_MEASURE:
 *
 * The "Data-Measure" stock icon.
 * <inlinegraphic fileref="gwy_data_measure-24.png" format="PNG"/>
 *
 * Since: 2.3
 **/

/**
 * GWY_STOCK_DECONVOLVE:
 *
 * The "Deconvolve" stock icon.
 * <inlinegraphic fileref="gwy_deconvolve-24.png" format="PNG"/>
 *
 * Since: 2.52
 **/

/**
 * GWY_STOCK_DISCONNECTED:
 *
 * The "Disconnected" stock icon.
 * <inlinegraphic fileref="gwy_disconnected-24.png" format="PNG"/>
 *
 * Since: 2.48
 **/

/**
 * GWY_STOCK_DISPLACEMENT_FIELD:
 *
 * The "Displacement-Field" stock icon.
 * <inlinegraphic fileref="gwy_displacement_field-24.png" format="PNG"/>
 *
 * Since: 2.61
 **/

/**
 * GWY_STOCK_DISTANCE:
 *
 * The "Distance" stock icon.
 * <inlinegraphic fileref="gwy_distance-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_DISTANCE_TRANSFORM:
 *
 * The "Distance-Transform" stock icon.
 * <inlinegraphic fileref="gwy_distance_transform-24.png" format="PNG"/>
 *
 * Since: 2.46
 **/

/**
 * GWY_STOCK_DISTRIBUTION_ANGLE:
 *
 * The "Distribution-Angle" stock icon.
 * <inlinegraphic fileref="gwy_distribution_angle-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_DISTRIBUTION_SLOPE:
 *
 * The "Distribution-Slope" stock icon.
 * <inlinegraphic fileref="gwy_distribution_slope-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_DRIFT:
 *
 * The "Drift" stock icon.
 * <inlinegraphic fileref="gwy_drift-24.png" format="PNG"/>
 *
 * Since: 2.3
 **/

/**
 * GWY_STOCK_DWT:
 *
 * The "DWT" stock icon.
 * <inlinegraphic fileref="gwy_dwt-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_EDGE:
 *
 * The "Edge" stock icon.
 * <inlinegraphic fileref="gwy_edge-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_ENFORCE_DISTRIBUTION:
 *
 * The "Enforce-Distribution" stock icon.
 * <inlinegraphic fileref="gwy_enforce_distribution-24.png" format="PNG"/>
 *
 * Since: 2.46
 **/

/**
 * GWY_STOCK_ENTROPY:
 *
 * The "Entropy" stock icon.
 * <inlinegraphic fileref="gwy_entropy-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_EXTEND:
 *
 * The "Extend" stock icon.
 * <inlinegraphic fileref="gwy_extend-24.png" format="PNG"/>
 *
 * Since: 2.37
 **/

/**
 * GWY_STOCK_EXTRACT_PATH:
 *
 * The "Extract-Path" stock icon.
 * <inlinegraphic fileref="gwy_extract_path-24.png" format="PNG"/>
 *
 * Since: 2.46
 **/

/**
 * GWY_STOCK_FACET_ANALYSIS:
 *
 * The "Facet-Analysis" stock icon.
 * <inlinegraphic fileref="gwy_facet_analysis-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_FACET_LEVEL:
 *
 * The "Facet-Level" stock icon.
 * <inlinegraphic fileref="gwy_facet_level-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_FACET_MEASURE:
 *
 * The "Facet-Measure" stock icon.
 * <inlinegraphic fileref="gwy_facet_measure-24.png" format="PNG"/>
 *
 * Since: 2.54
 **/

/**
 * GWY_STOCK_FAVOURITE:
 *
 * The "Favourite" stock icon.
 * <inlinegraphic fileref="gwy_favourite-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_FFT:
 *
 * The "FFT" stock icon.
 * <inlinegraphic fileref="gwy_fft-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_FFT_2D:
 *
 * The "FFT-2D" stock icon.
 * <inlinegraphic fileref="gwy_fft_2d-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_FFT_FILTER_1D:
 *
 * The "FFT-Filter-1D" stock icon.
 * <inlinegraphic fileref="gwy_fft_filter_1d-24.png" format="PNG"/>
 *
 * Since: 2.48
 **/

/**
 * GWY_STOCK_FFT_FILTER_2D:
 *
 * The "FFT-Filter-2D" stock icon.
 * <inlinegraphic fileref="gwy_fft_filter_2d-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_FILTER:
 *
 * The "Filter" stock icon.
 * <inlinegraphic fileref="gwy_filter-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_FIND_PEAKS:
 *
 * The "Find-Peaks" stock icon.
 * <inlinegraphic fileref="gwy_find_peaks-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_FIT_SHAPE:
 *
 * The "Fit-Shape" stock icon.
 * <inlinegraphic fileref="gwy_fit_shape-24.png" format="PNG"/>
 *
 * Since: 2.48
 **/

/**
 * GWY_STOCK_FIX_ZERO:
 *
 * The "Fix-Zero" stock icon.
 * <inlinegraphic fileref="gwy_fix_zero-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_FLIP_DIAGONALLY:
 *
 * The "Flip-Diagonally" stock icon.
 * <inlinegraphic fileref="gwy_flip_diagonally-24.png" format="PNG"/>
 *
 * Since: 2.51
 **/

/**
 * GWY_STOCK_FLIP_HORIZONTALLY:
 *
 * The "Flip-Horizontally" stock icon.
 * <inlinegraphic fileref="gwy_flip_horizontally-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_FLIP_VERTICALLY:
 *
 * The "Flip-Vertically" stock icon.
 * <inlinegraphic fileref="gwy_flip_vertically-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_FRACTAL:
 *
 * The "Fractal" stock icon.
 * <inlinegraphic fileref="gwy_fractal-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_FRACTAL_CORRECTION:
 *
 * The "Fractal-Correction" stock icon.
 * <inlinegraphic fileref="gwy_fractal_correction-24.png" format="PNG"/>
 *
 * Since: 2.48
 **/

/**
 * GWY_STOCK_FRACTAL_MEASURE:
 *
 * The "Fractal-Measure" stock icon.
 * <inlinegraphic fileref="gwy_fractal_measure-24.png" format="PNG"/>
 *
 * Since: 2.49
 **/

/**
 * GWY_STOCK_FREQUENCY_SPLIT:
 *
 * The "Frequency-Split" stock icon.
 * <inlinegraphic fileref="gwy_frequency_split-24.png" format="PNG"/>
 *
 * Since: 2.54
 **/

/**
 * GWY_STOCK_GL_MATERIAL:
 *
 * The "GL-Material" stock icon.
 * <inlinegraphic fileref="gwy_gl_material-16.png" format="PNG"/>
 *
 * Since: 2.7
 **/

/**
 * GWY_STOCK_GRADIENT_HORIZONTAL:
 *
 * The "Gradient-Horizontal" stock icon.
 * <inlinegraphic fileref="gwy_gradient_horizontal-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRADIENT_VERTICAL:
 *
 * The "Gradient-Vertical" stock icon.
 * <inlinegraphic fileref="gwy_gradient_vertical-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAIN_BOUNDING_BOX:
 *
 * The "Grain-Bounding-Box" stock icon.
 * <inlinegraphic fileref="gwy_grain_bounding_box-24.png" format="PNG"/>
 *
 * Since: 2.61
 **/

/**
 * GWY_STOCK_GRAIN_CORRELATION:
 *
 * The "Grain-Correlation" stock icon.
 * <inlinegraphic fileref="gwy_grain_correlation-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_GRAIN_EXSCRIBED_CIRCLE:
 *
 * The "Grain-Exscribed-Circle" stock icon.
 * <inlinegraphic fileref="gwy_grain_exscribed_circle-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_GRAIN_INSCRIBED_BOX:
 *
 * The "Grain-Inscribed-Box" stock icon.
 * <inlinegraphic fileref="gwy_grain_inscribed_box-24.png" format="PNG"/>
 *
 * Since: 2.61
 **/

/**
 * GWY_STOCK_GRAIN_INSCRIBED_CIRCLE:
 *
 * The "Grain-Inscribed-Circle" stock icon.
 * <inlinegraphic fileref="gwy_grain_inscribed_circle-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_GRAINS:
 *
 * The "Grains" stock icon.
 * <inlinegraphic fileref="gwy_grains-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAINS_EDGE:
 *
 * The "Grains-Edge" stock icon.
 * <inlinegraphic fileref="gwy_grains_edge-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_GRAINS_EDGE_REMOVE:
 *
 * The "Grains-Edge-Remove" stock icon.
 * <inlinegraphic fileref="gwy_grains_edge_remove-24.png" format="PNG"/>
 *
 * Since: 2.46
 **/

/**
 * GWY_STOCK_GRAINS_GRAPH:
 *
 * The "Grains-Graph" stock icon.
 * <inlinegraphic fileref="gwy_grains_graph-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAINS_MEASURE:
 *
 * The "Grains-Measure" stock icon.
 * <inlinegraphic fileref="gwy_grains_measure-24.png" format="PNG"/>
 *
 * Since: 2.7
 **/

/**
 * GWY_STOCK_GRAINS_OTSU:
 *
 * The "Grains-Otsu" stock icon.
 * <inlinegraphic fileref="gwy_grains_otsu-24.png" format="PNG"/>
 *
 * Since: 2.52
 **/

/**
 * GWY_STOCK_GRAINS_REMOVE:
 *
 * The "Grains-Remove" stock icon.
 * <inlinegraphic fileref="gwy_grains_remove-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAINS_STATISTICS:
 *
 * The "Grains-Statistics" stock icon.
 * <inlinegraphic fileref="gwy_grains_statistics-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_GRAINS_WATER:
 *
 * The "Grains-Water" stock icon.
 * <inlinegraphic fileref="gwy_grains_water-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAPH:
 *
 * The "Graph" stock icon.
 * <inlinegraphic fileref="gwy_graph-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAPH_ALIGN:
 *
 * The "Graph-Align" stock icon.
 * <inlinegraphic fileref="gwy_graph_align-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_GRAPH_CUT:
 *
 * The "Graph-Cut" stock icon.
 * <inlinegraphic fileref="gwy_graph_cut-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_GRAPH_DOS:
 *
 * The "Graph-Dos" stock icon.
 * <inlinegraphic fileref="gwy_graph_dos-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_GRAPH_EXPORT_ASCII:
 *
 * The "Graph-Export-Ascii" stock icon.
 * <inlinegraphic fileref="gwy_graph_export_ascii-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_GRAPH_EXPORT_PNG:
 *
 * The "Graph-Export-PNG" stock icon.
 * <inlinegraphic fileref="gwy_graph_export_png-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_GRAPH_EXPORT_VECTOR:
 *
 * The "Graph-Export-Vector" stock icon.
 * <inlinegraphic fileref="gwy_graph_export_vector-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_GRAPH_FD:
 *
 * The "Graph-FD" stock icon.
 * <inlinegraphic fileref="gwy_graph_fd-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_GRAPH_FILTER:
 *
 * The "Graph-Filter" stock icon.
 * <inlinegraphic fileref="gwy_graph_filter-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_GRAPH_FUNCTION:
 *
 * The "Graph-Function" stock icon.
 * <inlinegraphic fileref="gwy_graph_function-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAPH_HALFGAUSS:
 *
 * The "Graph-Halfgauss" stock icon.
 * <inlinegraphic fileref="gwy_graph_halfgauss-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAPH_LEVEL:
 *
 * The "Graph-Level" stock icon.
 * <inlinegraphic fileref="gwy_graph_level-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAPH_MEASURE:
 *
 * The "Graph-Measure" stock icon.
 * <inlinegraphic fileref="gwy_graph_measure-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAPH_PALETTE:
 *
 * The "Graph-Palette" stock icon.
 * <inlinegraphic fileref="gwy_graph_palette-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAPH_PERIOD_MEASURE:
 *
 * The "Graph-Period-Measure" stock icon.
 * <inlinegraphic fileref="gwy_graph_period_measure-24.png" format="PNG"/>
 *
 * Since: 2.63
 **/

/**
 * GWY_STOCK_GRAPH_POINTER:
 *
 * The "Graph-Pointer" stock icon.
 * <inlinegraphic fileref="gwy_graph_pointer-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAPH_RULER:
 *
 * The "Graph-Ruler" stock icon.
 * <inlinegraphic fileref="gwy_graph_ruler-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAPH_STATISTICS:
 *
 * The "Graph-Statistics" stock icon.
 * <inlinegraphic fileref="gwy_graph_statistics-24.png" format="PNG"/>
 *
 * Since: 2.54
 **/

/**
 * GWY_STOCK_GRAPH_TERRACE_MEASURE:
 *
 * The "Graph-Terrace-Measure" stock icon.
 * <inlinegraphic fileref="gwy_graph_terrace_measure-24.png" format="PNG"/>
 *
 * Since: 2.54
 **/

/**
 * GWY_STOCK_GRAPH_VERTICAL:
 *
 * The "Graph-Vertical" stock icon.
 * <inlinegraphic fileref="gwy_graph_vertical-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAPH_ZOOM_FIT:
 *
 * The "Graph-Zoom-Fit" stock icon.
 * <inlinegraphic fileref="gwy_graph_zoom_fit-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAPH_ZOOM_IN:
 *
 * The "Graph-Zoom-In" stock icon.
 * <inlinegraphic fileref="gwy_graph_zoom_in-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GRAPH_ZOOM_OUT:
 *
 * The "Graph-Zoom-Out" stock icon.
 * <inlinegraphic fileref="gwy_graph_zoom_out-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_GWYDDION:
 *
 * The "Gwyddion" stock icon.
 * <inlinegraphic fileref="gwy_gwyddion-60.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_HOLD_SELECTION_CLEAR:
 *
 * The "Hold-Selection-Clear" stock icon.
 * <inlinegraphic fileref="gwy_hold_selection_clear-18.png" format="PNG"/>
 *
 * Since: 2.63
 **/

/**
 * GWY_STOCK_HOLD_SELECTION_REPLACE:
 *
 * The "Hold-Selection-Replace" stock icon.
 * <inlinegraphic fileref="gwy_hold_selection_replace-18.png" format="PNG"/>
 *
 * Since: 2.63
 **/

/**
 * GWY_STOCK_HOUGH:
 *
 * The "Hough" stock icon.
 * <inlinegraphic fileref="gwy_hough-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_IMAGE_RELATION:
 *
 * The "Image-Relation" stock icon.
 * <inlinegraphic fileref="gwy_image_relation-24.png" format="PNG"/>
 *
 * Since: 2.54
 **/

/**
 * GWY_STOCK_IMMERSE:
 *
 * The "Immerse" stock icon.
 * <inlinegraphic fileref="gwy_immerse-24.png" format="PNG"/>
 *
 * Since: 2.3
 **/

/**
 * GWY_STOCK_ISO_ROUGHNESS:
 *
 * The "Iso-Roughness" stock icon.
 * <inlinegraphic fileref="gwy_iso_roughness-24.png" format="PNG"/>
 *
 * Since: 2.7
 **/

/**
 * GWY_STOCK_ITALIC:
 *
 * The "Italic" stock icon.
 * <inlinegraphic fileref="gwy_italic-20.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_LESS:
 *
 * The "Less" stock icon.
 * <inlinegraphic fileref="gwy_less-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_LEVEL:
 *
 * The "Level" stock icon.
 * <inlinegraphic fileref="gwy_level-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_LEVEL_FLATTEN_BASE:
 *
 * The "Level-Flatten-Base" stock icon.
 * <inlinegraphic fileref="gwy_level_flatten_base-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_LEVEL_MEDIAN:
 *
 * The "Level-Median" stock icon.
 * <inlinegraphic fileref="gwy_level_median-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_LEVEL_TRIANGLE:
 *
 * The "Level-Triangle" stock icon.
 * <inlinegraphic fileref="gwy_level_triangle-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_LIGHT_ROTATE:
 *
 * The "Light-Rotate" stock icon.
 * <inlinegraphic fileref="gwy_light_rotate-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_LIMIT_RANGE:
 *
 * The "Limit-Range" stock icon.
 * <inlinegraphic fileref="gwy_limit_range-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_LINE_LEVEL:
 *
 * The "Line-Level" stock icon.
 * <inlinegraphic fileref="gwy_line_level-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_LOAD_DEBUG:
 *
 * The "Load-Debug" stock icon.
 * <inlinegraphic fileref="gwy_load_debug-20.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_LOAD_INFO:
 *
 * The "Load-Info" stock icon.
 * <inlinegraphic fileref="gwy_load_info-20.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_LOAD_WARNING:
 *
 * The "Load-Warning" stock icon.
 * <inlinegraphic fileref="gwy_load_warning-20.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_LOCAL_SLOPE:
 *
 * The "Local-Slope" stock icon.
 * <inlinegraphic fileref="gwy_local_slope-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_LOGSCALE_HORIZONTAL:
 *
 * The "Logscale-Horizontal" stock icon.
 * <inlinegraphic fileref="gwy_logscale_horizontal-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_LOGSCALE_VERTICAL:
 *
 * The "Logscale-Vertical" stock icon.
 * <inlinegraphic fileref="gwy_logscale_vertical-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MARK_OUTLIERS:
 *
 * The "Mark-Outliers" stock icon.
 * <inlinegraphic fileref="gwy_mark_outliers-24.png" format="PNG"/>
 *
 * Since: 2.48
 **/

/**
 * GWY_STOCK_MARK_SCARS:
 *
 * The "Mark-Scars" stock icon.
 * <inlinegraphic fileref="gwy_mark_scars-24.png" format="PNG"/>
 *
 * Since: 2.48
 **/

/**
 * GWY_STOCK_MARK_WITH:
 *
 * The "Mark-With" stock icon.
 * <inlinegraphic fileref="gwy_mark_with-24.png" format="PNG"/>
 *
 * Since: 2.37
 **/

/**
 * GWY_STOCK_MASK:
 *
 * The "Mask" stock icon.
 * <inlinegraphic fileref="gwy_mask-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_ADD:
 *
 * The "Mask-Add" stock icon.
 * <inlinegraphic fileref="gwy_mask_add-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_CIRCLE:
 *
 * The "Mask-Circle" stock icon.
 * <inlinegraphic fileref="gwy_mask_circle-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_CIRCLE_EXCLUSIVE:
 *
 * The "Mask-Circle-Exclusive" stock icon.
 * <inlinegraphic fileref="gwy_mask_circle_exclusive-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_CIRCLE_INCLUSIVE:
 *
 * The "Mask-Circle-Inclusive" stock icon.
 * <inlinegraphic fileref="gwy_mask_circle_inclusive-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_DISTRIBUTE:
 *
 * The "Mask-Distribute" stock icon.
 * <inlinegraphic fileref="gwy_mask_distribute-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_MASK_EDITOR:
 *
 * The "Mask-Editor" stock icon.
 * <inlinegraphic fileref="gwy_mask_editor-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_EXCLUDE:
 *
 * The "Mask-Exclude" stock icon.
 * <inlinegraphic fileref="gwy_mask_exclude-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_EXCLUDE_CIRCLE:
 *
 * The "Mask-Exclude-Circle" stock icon.
 * <inlinegraphic fileref="gwy_mask_exclude_circle-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_EXTRACT:
 *
 * The "Mask-Extract" stock icon.
 * <inlinegraphic fileref="gwy_mask_extract-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_MASK_FILL_DRAW:
 *
 * The "Mask-Fill-Draw" stock icon.
 * <inlinegraphic fileref="gwy_mask_fill_draw-24.png" format="PNG"/>
 *
 * Since: 2.22
 **/

/**
 * GWY_STOCK_MASK_FILL_ERASE:
 *
 * The "Mask-Fill-Erase" stock icon.
 * <inlinegraphic fileref="gwy_mask_fill_erase-24.png" format="PNG"/>
 *
 * Since: 2.22
 **/

/**
 * GWY_STOCK_MASK_GROW:
 *
 * The "Mask-Grow" stock icon.
 * <inlinegraphic fileref="gwy_mask_grow-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_INTERSECT:
 *
 * The "Mask-Intersect" stock icon.
 * <inlinegraphic fileref="gwy_mask_intersect-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_INVERT:
 *
 * The "Mask-Invert" stock icon.
 * <inlinegraphic fileref="gwy_mask_invert-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_LINE:
 *
 * The "Mask-Line" stock icon.
 * <inlinegraphic fileref="gwy_mask_line-24.png" format="PNG"/>
 *
 * Since: 2.7
 **/

/**
 * GWY_STOCK_MASK_MORPH:
 *
 * The "Mask-Morph" stock icon.
 * <inlinegraphic fileref="gwy_mask_morph-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_MASK_NOISIFY:
 *
 * The "Mask-Noisify" stock icon.
 * <inlinegraphic fileref="gwy_mask_noisify-24.png" format="PNG"/>
 *
 * Since: 2.61
 **/

/**
 * GWY_STOCK_MASK_PAINT_DRAW:
 *
 * The "Mask-Paint-Draw" stock icon.
 * <inlinegraphic fileref="gwy_mask_paint_draw-24.png" format="PNG"/>
 *
 * Since: 2.22
 **/

/**
 * GWY_STOCK_MASK_PAINT_ERASE:
 *
 * The "Mask-Paint-Erase" stock icon.
 * <inlinegraphic fileref="gwy_mask_paint_erase-24.png" format="PNG"/>
 *
 * Since: 2.22
 **/

/**
 * GWY_STOCK_MASK_RECT_EXCLUSIVE:
 *
 * The "Mask-Rect-Exclusive" stock icon.
 * <inlinegraphic fileref="gwy_mask_rect_exclusive-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_RECT_INCLUSIVE:
 *
 * The "Mask-Rect-Inclusive" stock icon.
 * <inlinegraphic fileref="gwy_mask_rect_inclusive-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_REMOVE:
 *
 * The "Mask-Remove" stock icon.
 * <inlinegraphic fileref="gwy_mask_remove-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_SET:
 *
 * The "Mask-Set" stock icon.
 * <inlinegraphic fileref="gwy_mask_set-24.png" format="PNG"/>
 *
 * Since: 2.49
 **/

/**
 * GWY_STOCK_MASK_SHIFT:
 *
 * The "Mask-Shift" stock icon.
 * <inlinegraphic fileref="gwy_mask_shift-24.png" format="PNG"/>
 *
 * Since: 2.57
 **/

/**
 * GWY_STOCK_MASK_SHRINK:
 *
 * The "Mask-Shrink" stock icon.
 * <inlinegraphic fileref="gwy_mask_shrink-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_SUBTRACT:
 *
 * The "Mask-Subtract" stock icon.
 * <inlinegraphic fileref="gwy_mask_subtract-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MASK_THIN:
 *
 * The "Mask-Thin" stock icon.
 * <inlinegraphic fileref="gwy_mask_thin-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_MEASURE_LATTICE:
 *
 * The "Measure-Lattice" stock icon.
 * <inlinegraphic fileref="gwy_measure_lattice-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_MERGE:
 *
 * The "Merge" stock icon.
 * <inlinegraphic fileref="gwy_merge-24.png" format="PNG"/>
 *
 * Since: 2.3
 **/

/**
 * GWY_STOCK_MFM_CONVERT_TO_FORCE:
 *
 * The "MFM-Convert-To-Force" stock icon.
 * <inlinegraphic fileref="gwy_mfm_convert_to_force-24.png" format="PNG"/>
 *
 * Since: 2.52
 **/

/**
 * GWY_STOCK_MFM_CURRENT_LINE:
 *
 * The "MFM-Current-Line" stock icon.
 * <inlinegraphic fileref="gwy_mfm_current_line-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_MFM_FIELD_FIND_SHIFT:
 *
 * The "MFM-Field-Find-Shift" stock icon.
 * <inlinegraphic fileref="gwy_mfm_field_find_shift-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_MFM_FIELD_SHIFT:
 *
 * The "MFM-Field-Shift" stock icon.
 * <inlinegraphic fileref="gwy_mfm_field_shift-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_MFM_PARALLEL:
 *
 * The "MFM-Parallel" stock icon.
 * <inlinegraphic fileref="gwy_mfm_parallel-24.png" format="PNG"/>
 *
 * Since: 2.52
 **/

/**
 * GWY_STOCK_MFM_PERPENDICULAR:
 *
 * The "MFM-Perpendicular" stock icon.
 * <inlinegraphic fileref="gwy_mfm_perpendicular-24.png" format="PNG"/>
 *
 * Since: 2.52
 **/

/**
 * GWY_STOCK_MORE:
 *
 * The "More" stock icon.
 * <inlinegraphic fileref="gwy_more-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_MUTUAL_CROP:
 *
 * The "Mutual-Crop" stock icon.
 * <inlinegraphic fileref="gwy_mutual_crop-24.png" format="PNG"/>
 *
 * Since: 2.46
 **/

/**
 * GWY_STOCK_NEURAL_APPLY:
 *
 * The "Neural-Apply" stock icon.
 * <inlinegraphic fileref="gwy_neural_apply-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_NEURAL_TRAIN:
 *
 * The "Neural-Train" stock icon.
 * <inlinegraphic fileref="gwy_neural_train-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_NEXT:
 *
 * The "Next" stock icon.
 * <inlinegraphic fileref="gwy_next-24.png" format="PNG"/>
 *
 * Since: 2.49
 **/

/**
 * GWY_STOCK_NULL_OFFSETS:
 *
 * The "Null-Offsets" stock icon.
 * <inlinegraphic fileref="gwy_null_offsets-24.png" format="PNG"/>
 *
 * Since: 2.48
 **/

/**
 * GWY_STOCK_PALETTES:
 *
 * The "Palettes" stock icon.
 * <inlinegraphic fileref="gwy_palettes-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_PATH_LEVEL:
 *
 * The "Path-Level" stock icon.
 * <inlinegraphic fileref="gwy_path_level-24.png" format="PNG"/>
 *
 * Since: 2.7
 **/

/**
 * GWY_STOCK_PERSPECTIVE_DISTORT:
 *
 * The "Perspective-Distort" stock icon.
 * <inlinegraphic fileref="gwy_perspective_distort-24.png" format="PNG"/>
 *
 * Since: 2.61
 **/

/**
 * GWY_STOCK_POINTER_MEASURE:
 *
 * The "Pointer-Measure" stock icon.
 * <inlinegraphic fileref="gwy_pointer_measure-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_POLY_DISTORT:
 *
 * The "Poly-Distort" stock icon.
 * <inlinegraphic fileref="gwy_poly_distort-24.png" format="PNG"/>
 *
 * Since: 2.46
 **/

/**
 * GWY_STOCK_POLYNOM:
 *
 * The "Polynom" stock icon.
 * <inlinegraphic fileref="gwy_polynom-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_POLYNOM_LEVEL:
 *
 * The "Polynom-Level" stock icon.
 * <inlinegraphic fileref="gwy_polynom_level-24.png" format="PNG"/>
 *
 * Since: 2.29
 **/

/**
 * GWY_STOCK_PREVIOUS:
 *
 * The "Previous" stock icon.
 * <inlinegraphic fileref="gwy_previous-24.png" format="PNG"/>
 *
 * Since: 2.49
 **/

/**
 * GWY_STOCK_PROFILE:
 *
 * The "Profile" stock icon.
 * <inlinegraphic fileref="gwy_profile-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_PROFILE_MULTIPLE:
 *
 * The "Profile-Multiple" stock icon.
 * <inlinegraphic fileref="gwy_profile_multiple-24.png" format="PNG"/>
 *
 * Since: 2.57
 **/

/**
 * GWY_STOCK_PSDF_LOG_PHI:
 *
 * The "PSDF-Log-Phi" stock icon.
 * <inlinegraphic fileref="gwy_psdf_log_phi-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_PSDF_SECTION:
 *
 * The "PSDF-Section" stock icon.
 * <inlinegraphic fileref="gwy_psdf_section-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_PYGWY:
 *
 * The "Pygwy" stock icon.
 * <inlinegraphic fileref="gwy_pygwy-24.png" format="PNG"/>
 *
 * Since: 2.34
 **/

/**
 * GWY_STOCK_RADIAL_PROFILE:
 *
 * The "Radial-Profile" stock icon.
 * <inlinegraphic fileref="gwy_radial_profile-24.png" format="PNG"/>
 *
 * Since: 2.53
 **/

/**
 * GWY_STOCK_RANK_FILTER:
 *
 * The "Rank-Filter" stock icon.
 * <inlinegraphic fileref="gwy_rank_filter-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_RASTERIZE:
 *
 * The "Rasterize" stock icon.
 * <inlinegraphic fileref="gwy_rasterize-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_REMOVE_UNDER_MASK:
 *
 * The "Remove-Under-Mask" stock icon.
 * <inlinegraphic fileref="gwy_remove_under_mask-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_REVOLVE_ARC:
 *
 * The "Revolve-Arc" stock icon.
 * <inlinegraphic fileref="gwy_revolve_arc-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_REVOLVE_SPHERE:
 *
 * The "Revolve-Sphere" stock icon.
 * <inlinegraphic fileref="gwy_revolve_sphere-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_ROTATE:
 *
 * The "Rotate" stock icon.
 * <inlinegraphic fileref="gwy_rotate-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_ROTATE_180:
 *
 * The "Rotate-180" stock icon.
 * <inlinegraphic fileref="gwy_rotate_180-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_ROTATE_3D:
 *
 * The "Rotate-3D" stock icon.
 * <inlinegraphic fileref="gwy_rotate_3d-24.png" format="PNG"/>
 *
 * Since: 2.49
 **/

/**
 * GWY_STOCK_ROTATE_90_CCW:
 *
 * The "Rotate-90-CCW" stock icon.
 * <inlinegraphic fileref="gwy_rotate_90_ccw-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_ROTATE_90_CW:
 *
 * The "Rotate-90-CW" stock icon.
 * <inlinegraphic fileref="gwy_rotate_90_cw-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_SCALE:
 *
 * The "Scale" stock icon.
 * <inlinegraphic fileref="gwy_scale-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_SCALE_HORIZONTALLY:
 *
 * The "Scale-Horizontally" stock icon.
 * <inlinegraphic fileref="gwy_scale_horizontally-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_SCALE_VERTICALLY:
 *
 * The "Scale-Vertically" stock icon.
 * <inlinegraphic fileref="gwy_scale_vertically-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_SCARS:
 *
 * The "Scars" stock icon.
 * <inlinegraphic fileref="gwy_scars-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_SCIENTIFIC_NUMBER_FORMAT:
 *
 * The "Scientific-Number-Format" stock icon.
 * <inlinegraphic fileref="gwy_scientific_number_format-18.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_SELECTIONS:
 *
 * The "Selections" stock icon.
 * <inlinegraphic fileref="gwy_selections-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_SHADER:
 *
 * The "Shader" stock icon.
 * <inlinegraphic fileref="gwy_shader-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_SPECTRUM:
 *
 * The "Spectrum" stock icon.
 * <inlinegraphic fileref="gwy_spectrum-24.png" format="PNG"/>
 *
 * Since: 2.7
 **/

/**
 * GWY_STOCK_SPOT_REMOVE:
 *
 * The "Spot-Remove" stock icon.
 * <inlinegraphic fileref="gwy_spot_remove-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_SQUARE_SAMPLES:
 *
 * The "Square-Samples" stock icon.
 * <inlinegraphic fileref="gwy_square_samples-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_STAT_QUANTITIES:
 *
 * The "Stat-Quantities" stock icon.
 * <inlinegraphic fileref="gwy_stat_quantities-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_STITCH:
 *
 * The "Stitch" stock icon.
 * <inlinegraphic fileref="gwy_stitch-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_STRAIGHTEN_PATH:
 *
 * The "Straighten-Path" stock icon.
 * <inlinegraphic fileref="gwy_straighten_path-24.png" format="PNG"/>
 *
 * Since: 2.46
 **/

/**
 * GWY_STOCK_SUBSCRIPT:
 *
 * The "Subscript" stock icon.
 * <inlinegraphic fileref="gwy_subscript-20.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_SUPERSCRIPT:
 *
 * The "Superscript" stock icon.
 * <inlinegraphic fileref="gwy_superscript-20.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_SYNTHETIC_ANNEAL:
 *
 * The "Synthetic-Anneal" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_anneal-24.png" format="PNG"/>
 *
 * Since: 2.54
 **/

/**
 * GWY_STOCK_SYNTHETIC_BALLISTIC_DEPOSITION:
 *
 * The "Synthetic-Ballistic-Deposition" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_ballistic_deposition-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_SYNTHETIC_BROWNIAN_MOTION:
 *
 * The "Synthetic-Brownian-Motion" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_brownian_motion-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_SYNTHETIC_COLUMNAR:
 *
 * The "Synthetic-Columnar" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_columnar-24.png" format="PNG"/>
 *
 * Since: 2.37
 **/

/**
 * GWY_STOCK_SYNTHETIC_DIFFUSION:
 *
 * The "Synthetic-Diffusion" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_diffusion-24.png" format="PNG"/>
 *
 * Since: 2.38
 **/

/**
 * GWY_STOCK_SYNTHETIC_DISCS:
 *
 * The "Synthetic-Discs" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_discs-24.png" format="PNG"/>
 *
 * Since: 2.51
 **/

/**
 * GWY_STOCK_SYNTHETIC_DOMAINS:
 *
 * The "Synthetic-Domains" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_domains-24.png" format="PNG"/>
 *
 * Since: 2.37
 **/

/**
 * GWY_STOCK_SYNTHETIC_DUNES:
 *
 * The "Synthetic-Dunes" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_dunes-24.png" format="PNG"/>
 *
 * Since: 2.63
 **/

/**
 * GWY_STOCK_SYNTHETIC_FIBRES:
 *
 * The "Synthetic-Fibres" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_fibres-24.png" format="PNG"/>
 *
 * Since: 2.49
 **/

/**
 * GWY_STOCK_SYNTHETIC_LATTICE:
 *
 * The "Synthetic-Lattice" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_lattice-24.png" format="PNG"/>
 *
 * Since: 2.37
 **/

/**
 * GWY_STOCK_SYNTHETIC_LINE_NOISE:
 *
 * The "Synthetic-Line-Noise" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_line_noise-24.png" format="PNG"/>
 *
 * Since: 2.37
 **/

/**
 * GWY_STOCK_SYNTHETIC_NOISE:
 *
 * The "Synthetic-Noise" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_noise-24.png" format="PNG"/>
 *
 * Since: 2.37
 **/

/**
 * GWY_STOCK_SYNTHETIC_OBJECTS:
 *
 * The "Synthetic-Objects" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_objects-24.png" format="PNG"/>
 *
 * Since: 2.37
 **/

/**
 * GWY_STOCK_SYNTHETIC_PARTICLES:
 *
 * The "Synthetic-Particles" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_particles-24.png" format="PNG"/>
 *
 * Since: 2.37
 **/

/**
 * GWY_STOCK_SYNTHETIC_PATTERN:
 *
 * The "Synthetic-Pattern" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_pattern-24.png" format="PNG"/>
 *
 * Since: 2.37
 **/

/**
 * GWY_STOCK_SYNTHETIC_PHASES:
 *
 * The "Synthetic-Phases" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_phases-24.png" format="PNG"/>
 *
 * Since: 2.48
 **/

/**
 * GWY_STOCK_SYNTHETIC_PILEUP:
 *
 * The "Synthetic-Pileup" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_pileup-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_SYNTHETIC_PLATEAUS:
 *
 * The "Synthetic-Plateaus" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_plateaus-24.png" format="PNG"/>
 *
 * Since: 2.64
 **/

/**
 * GWY_STOCK_SYNTHETIC_SPECTRAL:
 *
 * The "Synthetic-Spectral" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_spectral-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_SYNTHETIC_TURING_PATTERN:
 *
 * The "Synthetic-Turing-Pattern" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_turing_pattern-24.png" format="PNG"/>
 *
 * Since: 2.54
 **/

/**
 * GWY_STOCK_SYNTHETIC_WAVES:
 *
 * The "Synthetic-Waves" stock icon.
 * <inlinegraphic fileref="gwy_synthetic_waves-24.png" format="PNG"/>
 *
 * Since: 2.37
 **/

/**
 * GWY_STOCK_TERRACE_MEASURE:
 *
 * The "Terrace-Measure" stock icon.
 * <inlinegraphic fileref="gwy_terrace_measure-24.png" format="PNG"/>
 *
 * Since: 2.54
 **/

/**
 * GWY_STOCK_TILT:
 *
 * The "Tilt" stock icon.
 * <inlinegraphic fileref="gwy_tilt-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_TIP_DILATION:
 *
 * The "Tip-Dilation" stock icon.
 * <inlinegraphic fileref="gwy_tip_dilation-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_TIP_EROSION:
 *
 * The "Tip-Erosion" stock icon.
 * <inlinegraphic fileref="gwy_tip_erosion-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_TIP_ESTIMATION:
 *
 * The "Tip-Estimation" stock icon.
 * <inlinegraphic fileref="gwy_tip_estimation-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_TIP_INDENT_ANALYZE:
 *
 * The "Tip-Indent-Analyze" stock icon.
 * <inlinegraphic fileref="gwy_tip_indent_analyze-24.png" format="PNG"/>
 *
 * Since: 2.46
 **/

/**
 * GWY_STOCK_TIP_LATERAL_FORCE:
 *
 * The "Tip-Lateral-Force" stock icon.
 * <inlinegraphic fileref="gwy_tip_lateral_force-24.png" format="PNG"/>
 *
 * Since: 2.46
 **/

/**
 * GWY_STOCK_TIP_MAP:
 *
 * The "Tip-Map" stock icon.
 * <inlinegraphic fileref="gwy_tip_map-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_TIP_MODEL:
 *
 * The "Tip-Model" stock icon.
 * <inlinegraphic fileref="gwy_tip_model-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_TIP_PID:
 *
 * The "Tip-Pid" stock icon.
 * <inlinegraphic fileref="gwy_tip_pid-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_UNROTATE:
 *
 * The "Unrotate" stock icon.
 * <inlinegraphic fileref="gwy_unrotate-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_VALUE_INVERT:
 *
 * The "Value-Invert" stock icon.
 * <inlinegraphic fileref="gwy_value_invert-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_VOLUME:
 *
 * The "Volume" stock icon.
 * <inlinegraphic fileref="gwy_volume-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_VOLUME_ARITHMETIC:
 *
 * The "Volume-Arithmetic" stock icon.
 * <inlinegraphic fileref="gwy_volume_arithmetic-24.png" format="PNG"/>
 *
 * Since: 2.51
 **/

/**
 * GWY_STOCK_VOLUME_CALIBRATE:
 *
 * The "Volume-Calibrate" stock icon.
 * <inlinegraphic fileref="gwy_volume_calibrate-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_VOLUME_DIMENSIONS:
 *
 * The "Volume-Dimensions" stock icon.
 * <inlinegraphic fileref="gwy_volume_dimensions-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_VOLUME_FD:
 *
 * The "Volume-FD" stock icon.
 * <inlinegraphic fileref="gwy_volume_fd-24.png" format="PNG"/>
 *
 * Since: 2.46
 **/

/**
 * GWY_STOCK_VOLUME_INVERT:
 *
 * The "Volume-Invert" stock icon.
 * <inlinegraphic fileref="gwy_volume_invert-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_VOLUME_KMEANS:
 *
 * The "Volume-Kmeans" stock icon.
 * <inlinegraphic fileref="gwy_volume_kmeans-24.png" format="PNG"/>
 *
 * Since: 2.46
 **/

/**
 * GWY_STOCK_VOLUME_KMEDIANS:
 *
 * The "Volume-Kmedians" stock icon.
 * <inlinegraphic fileref="gwy_volume_kmedians-24.png" format="PNG"/>
 *
 * Since: 2.46
 **/

/**
 * GWY_STOCK_VOLUME_LINE_STATS:
 *
 * The "Volume-Line-Stats" stock icon.
 * <inlinegraphic fileref="gwy_volume_line_stats-24.png" format="PNG"/>
 *
 * Since: 2.54
 **/

/**
 * GWY_STOCK_VOLUME_PLANE_STATS:
 *
 * The "Volume-Plane-Stats" stock icon.
 * <inlinegraphic fileref="gwy_volume_plane_stats-24.png" format="PNG"/>
 *
 * Since: 2.54
 **/

/**
 * GWY_STOCK_VOLUME_SLICE:
 *
 * The "Volume-Slice" stock icon.
 * <inlinegraphic fileref="gwy_volume_slice-24.png" format="PNG"/>
 *
 * Since: 2.46
 **/

/**
 * GWY_STOCK_VOLUME_SWAP_AXES:
 *
 * The "Volume-Swap-Axes" stock icon.
 * <inlinegraphic fileref="gwy_volume_swap_axes-24.png" format="PNG"/>
 *
 * Since: 2.54
 **/

/**
 * GWY_STOCK_VOLUMIZE:
 *
 * The "Volumize" stock icon.
 * <inlinegraphic fileref="gwy_volumize-24.png" format="PNG"/>
 *
 * Since: 2.46
 **/

/**
 * GWY_STOCK_VOLUMIZE_LAYERS:
 *
 * The "Volumize-Layers" stock icon.
 * <inlinegraphic fileref="gwy_volumize_layers-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_WRAP_VALUE:
 *
 * The "Wrap-Value" stock icon.
 * <inlinegraphic fileref="gwy_wrap_value-24.png" format="PNG"/>
 *
 * Since: 2.54
 **/

/**
 * GWY_STOCK_XY_DENOISE:
 *
 * The "Xy-Denoise" stock icon.
 * <inlinegraphic fileref="gwy_xy_denoise-24.png" format="PNG"/>
 *
 * Since: 2.48
 **/

/**
 * GWY_STOCK_XYZIZE:
 *
 * The "Xyzize" stock icon.
 * <inlinegraphic fileref="gwy_xyzize-24.png" format="PNG"/>
 *
 * Since: 2.50
 **/

/**
 * GWY_STOCK_ZERO_MAXIMUM:
 *
 * The "Zero-Maximum" stock icon.
 * <inlinegraphic fileref="gwy_zero_maximum-24.png" format="PNG"/>
 *
 * Since: 2.59
 **/

/**
 * GWY_STOCK_ZERO_MEAN:
 *
 * The "Zero-Mean" stock icon.
 * <inlinegraphic fileref="gwy_zero_mean-24.png" format="PNG"/>
 *
 * Since: 2.45
 **/

/**
 * GWY_STOCK_ZERO_UNDER_MASK:
 *
 * The "Zero-Under-Mask" stock icon.
 * <inlinegraphic fileref="gwy_zero_under_mask-24.png" format="PNG"/>
 *
 * Since: 2.61
 **/

/**
 * GWY_STOCK_ZOOM_1_1:
 *
 * The "Zoom-1:1" stock icon.
 * <inlinegraphic fileref="gwy_zoom_1_1-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_ZOOM_FIT:
 *
 * The "Zoom-Fit" stock icon.
 * <inlinegraphic fileref="gwy_zoom_fit-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_ZOOM_IN:
 *
 * The "Zoom-In" stock icon.
 * <inlinegraphic fileref="gwy_zoom_in-24.png" format="PNG"/>
 **/

/**
 * GWY_STOCK_ZOOM_OUT:
 *
 * The "Zoom-Out" stock icon.
 * <inlinegraphic fileref="gwy_zoom_out-24.png" format="PNG"/>
 **/

/* @@@ GENERATED STOCK LIST END @@@ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
