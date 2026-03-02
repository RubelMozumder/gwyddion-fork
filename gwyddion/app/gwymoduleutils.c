/*
 *  $Id: gwymoduleutils.c 25445 2023-06-15 16:24:52Z yeti-dn $
 *  Copyright (C) 2007-2023 David Necas (Yeti), Petr Klapetek.
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

#include "config.h"
#include <string.h>
#include <errno.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwylayer-mask.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwydgetutils.h>
#include "file.h"
#include "data-browser.h"
#include "settings.h"
#include "wait.h"
#include "dialog.h"
#include "param-internal.h"
#include "gwymoduleutils.h"

typedef struct {
    const gchar *data;
    gssize len;
} GwySaveAuxiliaryData;

typedef struct {
    GwySelection *selection;
    GwyParamTable *table;
    gdouble min;
    gdouble max;
    gulong selection_id;
    gulong param_id;
    gint id_from;
    gint id_to;
    gboolean in_update;
} GraphRangeData;

static void graph_range_selection_destroyed(gpointer user_data,
                                            GObject *selection);
static void graph_range_table_destroyed    (gpointer user_data,
                                            GObject *selection);

static gchar*
gwy_save_auxiliary_data_create(gpointer user_data,
                               gssize *data_len)
{
    GwySaveAuxiliaryData *savedata = (GwySaveAuxiliaryData*)user_data;

    *data_len = savedata->len;

    return (gchar*)savedata->data;
}

/**
 * gwy_save_auxiliary_data:
 * @title: File chooser dialog title.
 * @parent: Parent window for the file chooser dialog (may be %NULL).
 * @data_len: The length of @data in bytes.  Pass -1 if @data is text, it must be nul-terminated then and it will be
 *            saved in text mode (this matters if the operating system distinguishes between text and binary).
 *            A non-negative value causes the data to be saved as binary.
 * @data: The data to save.
 *
 * Saves a report or other auxiliary data to a user specified file.
 *
 * This is actually a simple gwy_save_auxiliary_with_callback() wrapper, see its description for details.
 *
 * Returns: %TRUE if the data was saved, %FALSE if it was not saved for any reason.
 *
 * Since: 2.3
 **/
gboolean
gwy_save_auxiliary_data(const gchar *title,
                        GtkWindow *parent,
                        gssize data_len,
                        const gchar *data)
{
    GwySaveAuxiliaryData savedata;

    g_return_val_if_fail(data, FALSE);
    savedata.data = data;
    savedata.len = data_len;

    return gwy_save_auxiliary_with_callback(title, parent, &gwy_save_auxiliary_data_create, NULL, &savedata);
}

/**
 * gwy_save_auxiliary_with_callback:
 * @title: File chooser dialog title.
 * @parent: Parent window for the file chooser dialog (may be %NULL).
 * @create: Function to create the data (it will not be called if the user cancels the saving).
 * @destroy: Function to destroy the data (if will be called iff @create will be called), it may be %NULL.
 * @user_data: User data passed to @create and @destroy.
 *
 * Saves a report or other auxiliary data to a user specified file.
 *
 * Returns: %TRUE if the data was saved, %FALSE if it was not saved for any reason (I/O error, cancellation, overwrite
 *          cancellation, etc.).
 *
 * Since: 2.3
 **/
gboolean
gwy_save_auxiliary_with_callback(const gchar *title,
                                 GtkWindow *parent,
                                 GwySaveAuxiliaryCreate create,
                                 GwySaveAuxiliaryDestroy destroy,
                                 gpointer user_data)
{
    gchar *data;
    gchar *filename_sys, *filename_utf8;
    gssize data_len;
    gint myerrno;
    GtkWidget *dialog, *chooser;
    gint response;
    FILE *fh;

    g_return_val_if_fail(!parent || GTK_IS_WINDOW(parent), FALSE);
    g_return_val_if_fail(create, FALSE);

    chooser = gtk_file_chooser_dialog_new(title, parent,
                                          GTK_FILE_CHOOSER_ACTION_SAVE,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                          GTK_STOCK_SAVE, GTK_RESPONSE_OK,
                                          NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(chooser), GTK_RESPONSE_OK);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(chooser), gwy_app_get_current_directory());
    response = gtk_dialog_run(GTK_DIALOG(chooser));
    filename_sys = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));

    if (!gwy_app_file_confirm_overwrite(chooser))
        response = GTK_RESPONSE_CANCEL;

    gtk_widget_destroy(chooser);

    if (!filename_sys || response != GTK_RESPONSE_OK) {
        g_free(filename_sys);
        return FALSE;
    }

    data_len = -1;
    data = create(user_data, &data_len);
    g_return_val_if_fail(data, FALSE);

    if ((fh = gwy_fopen(filename_sys, "wb"))) {
        gchar *mydata = NULL;

        /* Write everything in binary and just convert the EOLs by manually.
         * This seems to actually work as we want. */
        if (data_len <= 0) {
#ifdef G_OS_WIN32
            mydata = gwy_strreplace(data, "\n", "\r\n", (gsize)-1);
            data_len = strlen(mydata);
#else
            data_len = strlen(data);
#endif
        }
        if (fwrite(mydata ? mydata : data, data_len, 1, fh) != 1) {
            myerrno = errno;
            /* This is just best-effort clean-up */
            fclose(fh);
            g_unlink(filename_sys);
            fh = NULL;
        }
        else
            myerrno = 0;  /* GCC */

        g_free(mydata);
    }
    else
        myerrno = errno;

    if (destroy)
        destroy(data, user_data);

    if (fh) {
        /* Everything went all right. */
        fclose(fh);
        gwy_app_set_current_directory(filename_sys);
        g_free(filename_sys);
        return TRUE;
    }

    filename_utf8 = g_filename_to_utf8(filename_sys, -1, NULL, NULL, NULL);
    dialog = gtk_message_dialog_new(parent, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                    _("Saving of `%s' failed"), filename_utf8);
    g_free(filename_sys);
    g_free(filename_utf8);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             _("Cannot write to file: %s."), g_strerror(myerrno));
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return FALSE;
}

/**
 * gwy_module_data_load:
 * @modname: Module name (determines the subdirectory).
 * @filename: Name of the file to load.  In GLib encoding, but it really should be just ASCII.
 * @contents: Location to store the allocated file contents.  Use g_free() to free it.  It is set to %NULL when the
 *            file cannot be loaded.
 * @length: Location to the length of the contents in bytes, or %NULL.
 * @error: Location for error, or %NULL.
 *
 * Load module data file from the user directory.
 *
 * The function wraps g_file_get_contents(), forming the full file name automatically.
 *
 * The error can be from %G_FILE_ERROR domain.  Usually, however, you only need the return value and consider the file
 * simply not existing yet when the function fails.
 *
 * Returns: %TRUE if the file was loaded.
 *
 * Since: 2.51
 **/
gboolean
gwy_module_data_load(const gchar *modname,
                     const gchar *filename,
                     gchar **contents,
                     gsize *length,
                     GError **error)
{
    gchar *path;
    gboolean ok;

    path = g_build_filename(gwy_get_user_dir(), modname, filename, NULL);
    /* Do not bother creating the directory in the load function.  We only
     * need it in the save function. */
    ok = g_file_get_contents(path, contents, length, error);
    g_free(path);

    return ok;
}

static gboolean
ensure_module_directory(const gchar *userdir,
                        const gchar *modname,
                        GError **error)
{
    gchar *path;

    path = g_build_filename(userdir, modname, NULL);
    if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        g_free(path);
        return TRUE;
    }

    if (g_mkdir(path, 0700) == 0) {
        g_free(path);
        return TRUE;
    }

    g_set_error_literal(error, G_FILE_ERROR, g_file_error_from_errno(errno), g_strerror(errno));
    g_free(path);
    return FALSE;
}

/**
 * gwy_module_data_save:
 * @modname: Module name (determines the subdirectory).
 * @filename: Name of the file to save.  In GLib encoding, but it really should be just ASCII.
 * @contents: File contents to write.
 * @length: Length of @contents, or -1 if it is a NUL-terminated string.
 * @error: Location for error, or %NULL.
 *
 * Saves module data file to the user directory.
 *
 * The function wraps g_file_set_contents(), forming the full file name automatically and handling subdirectory
 * creation.
 *
 * The error can be from %G_FILE_ERROR domain.
 *
 * Returns: %TRUE if the file was saved.
 *
 * Since: 2.51
 **/
gboolean
gwy_module_data_save(const gchar *modname,
                     const gchar *filename,
                     gchar *contents,
                     gssize length,
                     GError **error)
{
    const gchar *userdir;
    gchar *path;
    gboolean ok;

    userdir = gwy_get_user_dir();
    if (!ensure_module_directory(userdir, modname, error))
        return FALSE;

    path = g_build_filename(userdir, modname, filename, NULL);
    ok = g_file_set_contents(path, contents, length, error);
    g_free(path);

    return ok;
}

/**
 * gwy_module_data_fopen:
 * @modname: Module name (determines the subdirectory).
 * @filename: Name of the file to save.  In GLib encoding, but it really should be just ASCII.
 * @mode: File open mode, as in fopen().
 * @error: Location for error, or %NULL.
 *
 * Opens a module data file in the user directory.
 *
 * The function wraps gwy_fopen(), forming the full file name automatically and handling subdirectory creation.
 *
 * The error can be from %G_FILE_ERROR domain.
 *
 * Returns: A file handle if the file was opened as requested, %NULL on failure.
 *
 * Since: 2.51
 **/
FILE*
gwy_module_data_fopen(const gchar *modname,
                      const gchar *filename,
                      const gchar *mode,
                      GError **error)
{
    const gchar *userdir;
    gchar *path;
    FILE *fh;

    userdir = gwy_get_user_dir();
    if (!ensure_module_directory(userdir, modname, error))
        return FALSE;

    path = g_build_filename(userdir, modname, filename, NULL);
    if (!(fh = gwy_fopen(path, mode)))
        g_set_error_literal(error, G_FILE_ERROR, g_file_error_from_errno(errno), g_strerror(errno));
    g_free(path);

    return fh;
}

/**
 * gwy_set_data_preview_size:
 * @data_view: A data view used for module preview.
 * @max_size: Maximum allowed @data_view size (width and height).
 *
 * Sets up data view zoom to not exceed specified size.
 *
 * Before calling this function, data keys have be set, data fields and layers have to be present and physically
 * square mode set in the container. Sizing of both pixel-wise square and physically square displays is performed
 * correctly.
 *
 * Since: 2.7
 **/
void
gwy_set_data_preview_size(GwyDataView *data_view,
                          gint max_size)
{
    GwyContainer *container;
    GwyDataField *data_field;
    GwyPixmapLayer *layer;
    gdouble zoomval, scale, xreal, yreal;
    gboolean realsquare;
    gint xres, yres;
    const gchar *prefix;
    gchar *key;

    g_return_if_fail(GWY_IS_DATA_VIEW(data_view));
    g_return_if_fail(max_size >= 2);

    container = gwy_data_view_get_data(data_view);
    g_return_if_fail(GWY_IS_CONTAINER(container));

    layer = gwy_data_view_get_base_layer(data_view);
    g_return_if_fail(GWY_IS_PIXMAP_LAYER(layer));
    prefix = gwy_pixmap_layer_get_data_key(layer);
    g_return_if_fail(prefix);

    data_field = gwy_container_get_object_by_name(container, prefix);
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));

    prefix = gwy_data_view_get_data_prefix(data_view);
    g_return_if_fail(prefix);
    key = g_strconcat(prefix, "/realsquare", NULL);
    realsquare = FALSE;
    gwy_container_gis_boolean_by_name(container, key, &realsquare);
    g_free(key);

    xres = gwy_data_field_get_xres(data_field);
    yres = gwy_data_field_get_yres(data_field);
    if (!realsquare)
        zoomval = max_size/(gdouble)MAX(xres, yres);
    else {
        xreal = gwy_data_field_get_xreal(data_field);
        yreal = gwy_data_field_get_yreal(data_field);
        scale = MAX(xres/xreal, yres/yreal);
        zoomval = max_size/(scale*MAX(xreal, yreal));
    }
    gwy_data_view_set_zoom(data_view, zoomval);
}

static gboolean
clear_data_id(GwyAppDataId *id)
{
    id->datano = 0;
    id->id = -1;
    return FALSE;
}

/**
 * gwy_app_data_id_verify_channel:
 * @id: Numerical identifiers of a channel in data managed by the data browser.
 *
 * Checks if numerical channel identifiers correspond to an existing channel.
 *
 * If either the data contained referenced in @id or the channel does not exist the structure is cleared to
 * %GWY_APP_DATA_ID_NONE and the function returns %FALSE.  If it represents an existing channel it is kept intact and
 * the function returns %TRUE.
 *
 * Returns: Whether @id refers to an existing channel now.
 *
 * Since: 2.41
 **/
gboolean
gwy_app_data_id_verify_channel(GwyAppDataId *id)
{
    GwyContainer *container;
    GObject *object;
    GQuark quark;

    g_return_val_if_fail(id, FALSE);

    container = gwy_app_data_browser_get(id->datano);
    if (!container)
        return clear_data_id(id);

    quark = gwy_app_get_data_key_for_id(id->id);
    if (!gwy_container_gis_object(container, quark, &object))
        return clear_data_id(id);
    if (!GWY_IS_DATA_FIELD(object))
        return clear_data_id(id);

    return TRUE;
}

/**
 * gwy_app_data_id_verify_graph:
 * @id: Numerical identifiers of a graph in data managed by the data browser.
 *
 * Checks if numerical graph identifiers correspond to an existing graph.
 *
 * If either the data contained referenced in @id or the graph model does not exist the structure is cleared to
 * %GWY_APP_DATA_ID_NONE and the function returns %FALSE.  If it represents an existing graph it is kept intact and
 * the function returns %TRUE.
 *
 * Returns: Whether @id refers to an existing graph now.
 *
 * Since: 2.41
 **/
gboolean
gwy_app_data_id_verify_graph(GwyAppDataId *id)
{
    GwyContainer *container;
    GObject *object;
    GQuark quark;

    g_return_val_if_fail(id, FALSE);

    container = gwy_app_data_browser_get(id->datano);
    if (!container)
        return clear_data_id(id);

    quark = gwy_app_get_graph_key_for_id(id->id);
    if (!gwy_container_gis_object(container, quark, &object))
        return clear_data_id(id);
    if (!GWY_IS_GRAPH_MODEL(object))
        return clear_data_id(id);

    return TRUE;
}

/**
 * gwy_app_data_id_verify_volume:
 * @id: Numerical identifiers of volume data in data managed by the data browser.
 *
 * Checks if numerical volume data identifiers correspond to existing volume data.
 *
 * If either the data contained referenced in @id or the volume data does not exist the structure is cleared to
 * %GWY_APP_DATA_ID_NONE and the function returns %FALSE.  If it represents existing volume data it is kept intact and
 * the function returns %TRUE.
 *
 * Returns: Whether @id refers to existing volume data now.
 *
 * Since: 2.41
 **/
gboolean
gwy_app_data_id_verify_volume(GwyAppDataId *id)
{
    GwyContainer *container;
    GObject *object;
    GQuark quark;

    g_return_val_if_fail(id, FALSE);

    container = gwy_app_data_browser_get(id->datano);
    if (!container)
        return clear_data_id(id);

    quark = gwy_app_get_brick_key_for_id(id->id);
    if (!gwy_container_gis_object(container, quark, &object))
        return clear_data_id(id);
    if (!GWY_IS_BRICK(object))
        return clear_data_id(id);

    return TRUE;
}

/**
 * gwy_app_data_id_verify_xyz:
 * @id: Numerical identifiers of XYZ data in data managed by the data browser.
 *
 * Checks if numerical XYZ data identifiers correspond to existing XYZ data.
 *
 * If either the data contained referenced in @id or the XYZ data does not exist the structure is cleared to
 * %GWY_APP_DATA_ID_NONE and the function returns %FALSE.  If it represents existing XYZ data it is kept intact and
 * the function returns %TRUE.
 *
 * Returns: Whether @id refers to existing XYZ data now.
 *
 * Since: 2.46
 **/
gboolean
gwy_app_data_id_verify_xyz(GwyAppDataId *id)
{
    GwyContainer *container;
    GObject *object;
    GQuark quark;

    g_return_val_if_fail(id, FALSE);

    container = gwy_app_data_browser_get(id->datano);
    if (!container)
        return clear_data_id(id);

    quark = gwy_app_get_surface_key_for_id(id->id);
    if (!gwy_container_gis_object(container, quark, &object))
        return clear_data_id(id);
    if (!GWY_IS_SURFACE(object))
        return clear_data_id(id);

    return TRUE;
}

/**
 * gwy_app_data_id_verify_curve_map:
 * @id: Numerical identifiers of curve map data in data managed by the data browser.
 *
 * Checks if numerical curve map data identifiers correspond to existing curve map data.
 *
 * If either the data contained referenced in @id or the curve map data does not exist the structure is cleared to
 * %GWY_APP_DATA_ID_NONE and the function returns %FALSE.  If it represents existing curve map data it is kept intact
 * and the function returns %TRUE.
 *
 * Returns: Whether @id refers to existing curve map data now.
 *
 * Since: 2.46
 **/
gboolean
gwy_app_data_id_verify_curve_map(GwyAppDataId *id)
{
    GwyContainer *container;
    GObject *object;
    GQuark quark;

    g_return_val_if_fail(id, FALSE);

    container = gwy_app_data_browser_get(id->datano);
    if (!container)
        return clear_data_id(id);

    quark = gwy_app_get_lawn_key_for_id(id->id);
    if (!gwy_container_gis_object(container, quark, &object))
        return clear_data_id(id);
    if (!GWY_IS_LAWN(object))
        return clear_data_id(id);

    return TRUE;
}

/**
 * gwy_app_data_id_verify_spectra:
 * @id: Numerical identifiers of spectra in data managed by the data browser.
 *
 * Checks if numerical spectra identifiers correspond to existing spectra.
 *
 * If either the data contained referenced in @id or the spectra does not exist the structure is cleared to
 * %GWY_APP_DATA_ID_NONE and the function returns %FALSE.  If it represents existing spectra it is kept intact and the
 * function return %TRUE.
 *
 * Returns: Whether @id refers to existing spectra now.
 *
 * Since: 2.41
 **/
gboolean
gwy_app_data_id_verify_spectra(GwyAppDataId *id)
{
    GwyContainer *container;
    GObject *object;
    GQuark quark;

    g_return_val_if_fail(id, FALSE);

    container = gwy_app_data_browser_get(id->datano);
    if (!container)
        return clear_data_id(id);

    quark = gwy_app_get_spectra_key_for_id(id->id);
    if (!gwy_container_gis_object(container, quark, &object))
        return clear_data_id(id);
    if (!GWY_IS_SPECTRA(object))
        return clear_data_id(id);

    return TRUE;
}

/**
 * gwy_app_add_graph_or_curves:
 * @gmodel: A graph model with curves to add.
 * @data: Data container where the graph would be added.
 * @target_graph: Graph where curves would be added.
 * @colorstep: Curve block size as in gwy_graph_model_append_curves().
 *
 * Puts the curves of a graph to another graph if possible, or adds the graph as new.
 *
 * If the units of @gmodel are compatible with the units of the graph identified by @target_graph the curves are
 * copied to the target graph with gwy_graph_model_append_curves().
 *
 * In all other cases, including when @target_graph does not refer to any existing graph, the graph model is added to
 * @data as a new graph.
 *
 * Either way, the caller usually need to release its own reference afterwards.
 *
 * This function is useful particularly in modules that create graphs and can be run non-interactively.
 *
 * Returns: The numerical identifier of the newly-created graph of one was created.  Value -1 is returned if curves
 *          were added to @target_graph.
 *
 * Since: 2.41
 **/
gint
gwy_app_add_graph_or_curves(GwyGraphModel *gmodel,
                            GwyContainer *data,
                            const GwyAppDataId *target_graph,
                            gint colorstep)
{
    GwyAppDataId tgtgraph = *target_graph;

    if (gwy_app_data_id_verify_graph(&tgtgraph)) {
        GQuark quark = gwy_app_get_graph_key_for_id(tgtgraph.id);
        GwyContainer *data2 = gwy_app_data_browser_get(tgtgraph.datano);
        GwyGraphModel *target_gmodel = gwy_container_get_object(data2, quark);

        g_return_val_if_fail(GWY_IS_GRAPH_MODEL(target_gmodel), -1);
        if (gwy_graph_model_units_are_compatible(gmodel, target_gmodel)) {
            gwy_graph_model_append_curves(target_gmodel, gmodel, colorstep);
            return -1;
        }
    }
    g_return_val_if_fail(GWY_IS_CONTAINER(data), FALSE);
    return gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);
}

/**
 * gwy_preview_surface_to_datafield:
 * @surface: A surface representing a XYZ data.
 * @dfield: A data field to fill with @surface preview.
 * @max_xres: Maximum width of the preview, it must be at least 2.
 * @max_yres: Maximum height of the preview, it must be at least 2.
 * @flags: Flags modifying the behaviour.
 *
 * Renders a preview of a XYZ data surface to a data field.
 *
 * Since: 2.46
 **/
void
gwy_preview_surface_to_datafield(GwySurface *surface,
                                 GwyDataField *dfield,
                                 gint max_xres, gint max_yres,
                                 GwyPreviewSurfaceFlags flags)
{
    gdouble q, h, xmin, xmax, ymin, ymax;
    guint n = gwy_surface_get_npoints(surface);
    gboolean do_fill = (flags & GWY_PREVIEW_SURFACE_FILL);
    gboolean want_densitymap = (flags & GWY_PREVIEW_SURFACE_DENSITY);
    gboolean xrange_normal, yrange_normal;
    GwyDataField *densitymap = NULL;
    guint xres, yres;

    gwy_surface_get_xrange(surface, &xmin, &xmax);
    xrange_normal = (xmin < xmax);

    gwy_surface_get_yrange(surface, &ymin, &ymax);
    yrange_normal = (ymin < ymax);

    if (!xrange_normal) {
        if (yrange_normal) {
            xmin = xmax - (ymax - ymin)/n;
            xmax = xmax + (ymax - ymin)/n;
        }
        else if (xmax) {
            xmin = 1.5*xmax;
            xmax = 0.5*xmax;
        }
        else {
            xmin = -1.0;
            xmax = 1.0;
        }
    }

    if (!yrange_normal) {
        if (xrange_normal) {
            ymin = ymax - (xmax - xmin)/n;
            ymax = ymax + (xmax - xmin)/n;
        }
        else if (ymax) {
            ymin = 0.5*ymax;
            ymax = 1.5*ymax;
        }
        else {
            ymin = -1.0;
            ymax = 1.0;
        }
    }

    if (do_fill) {
        h = fmax((xmax - xmin)/(max_xres - 1.0), (ymax - ymin)/(max_yres - 1.0));
        xmin -= 0.5*h;
        ymin -= 0.5*h;
        xmax += 0.5*h;
        ymax += 0.5*h;
        xres = GWY_ROUND((xmax - xmin)/h);
        xres = CLAMP(xres, 2, max_xres);
        yres = GWY_ROUND((ymax - ymin)/h);
        yres = CLAMP(yres, 2, max_yres);
    }
    else {
        q = (ymax - ymin)/(xmax - xmin);
        if (q <= 1.0) {
            yres = GWY_ROUND(sqrt(3.0*q*n));
            yres = MAX(yres, 2);
            h = (ymax - ymin)/yres;
            xres = GWY_ROUND((xmax - xmin)/h);
            if (CLAMP(xres, 2, max_xres) != xres) {
                xres = CLAMP(xres, 2, max_xres);
                h = (xmax - xmin)/xres;
                yres = (gint)ceil((ymax - ymin)/h);
            }
        }
        else {
            xres = GWY_ROUND(sqrt(3.0/q*n));
            xres = MAX(xres, 2);
            h = (xmax - xmin)/xres;
            yres = GWY_ROUND((ymax - ymin)/h);
            if (CLAMP(yres, 2, max_yres) != yres) {
                yres = CLAMP(yres, 2, max_yres);
                h = (ymax - ymin)/yres;
                xres = (gint)ceil((xmax - xmin)/h);
            }
        }

        xmin -= 0.5*h;
        ymin -= 0.5*h;
        xmax += 0.5*h;
        ymax += 0.5*h;
        if ((xmax - xmin)/xres < (ymax - ymin)/yres) {
            gdouble excess = (ymax - ymin)/yres*xres - (xmax - xmin);
            xmin -= 0.5*excess;
            xmax += 0.5*excess;
        }
        else {
            gdouble excess = (xmax - xmin)/xres*yres - (ymax - ymin);
            ymin -= 0.5*excess;
            ymax += 0.5*excess;
        }
    }

    gwy_data_field_resample(dfield, xres, yres, GWY_INTERPOLATION_NONE);
    gwy_data_field_set_xreal(dfield, xmax - xmin);
    gwy_data_field_set_yreal(dfield, ymax - ymin);
    gwy_data_field_set_xoffset(dfield, xmin);
    gwy_data_field_set_yoffset(dfield, ymin);
    if (want_densitymap)
        densitymap = gwy_data_field_new_alike(dfield, FALSE);

    gwy_data_field_average_xyz(dfield, densitymap, gwy_surface_get_data_const(surface), n);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(dfield), gwy_surface_get_si_unit_xy(surface));

    if (want_densitymap) {
        gwy_data_field_copy(densitymap, dfield, FALSE);
        GWY_OBJECT_UNREF(densitymap);
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), NULL);
    }
    else
        gwy_si_unit_assign(gwy_data_field_get_si_unit_z(dfield), gwy_surface_get_si_unit_z(surface));
}

/**
 * gwy_app_wait_preview_data_field:
 * @dfield: A data field used for preview.
 * @data: Data container with source image (or %NULL).
 * @id: Source image id (or -1).
 *
 * Helper function for progressive animated preview.
 *
 * This is a wrapper for gwy_app_wait_set_preview_widget() which creates a simple image preview displaying @dfield.
 * Call gwy_data_field_data_changed() on it to redraw the preview.
 *
 * No preview is created if progress bars are disabled, see gwy_app_wait_set_enabled().
 *
 * Returns: The created #GwyDataView with the preview (possibly %NULL if not preview was actually created).  Usually
 *          you can ignore it.
 *
 * Since: 2.59
 **/
GtkWidget*
gwy_app_wait_preview_data_field(GwyDataField *dfield,
                                GwyContainer *data,
                                gint id)
{
    GwyContainer *mydata;
    GtkWidget *preview;

    if (!gwy_app_wait_get_enabled())
        return NULL;

    mydata = gwy_container_new();
    gwy_container_set_object(mydata, gwy_app_get_data_key_for_id(0), dfield);
    preview = gwy_create_preview(mydata, 0, 360, FALSE);
    if (data && id >= 0) {
        gwy_app_sync_data_items(data, mydata, id, 0, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                0);
    }
    gwy_app_wait_set_preview_widget(preview);

    return preview;
}

/**
 * gwy_run_subdialog:
 * @dialog: A data dialog to be run as modal.
 *
 * Runs a modal dialog transient for another another dialog, trying to work around bugs.
 *
 * If both dialogs are modal, bad things can happen in some graphical environments, such as the second being hidden
 * under the first one and inaccessible. This function attempts to work around this by temporarily setting the parent
 * dialog (the one @dialog is transient for) non-modal.
 *
 * If the dialog is not transient for anything or its parent is not modal it is just run normally.
 *
 * The function returns only after the dialog has been destroyed. Do not destroy it yourself.
 *
 * Returns: The response of @dialog.
 *
 * Since: 2.62
 **/
gint
gwy_run_subdialog(GtkDialog *dialog)
{
    GtkWindow *parent;
    gint response;

    g_return_val_if_fail(GTK_IS_DIALOG(dialog), GTK_RESPONSE_NONE);
    parent = gtk_window_get_transient_for(GTK_WINDOW(dialog));
    if (parent && GTK_IS_WINDOW(parent) && gtk_window_get_modal(parent))
        gtk_window_set_modal(GTK_WINDOW(parent), FALSE);
    else
        parent = NULL;

    response = gtk_dialog_run(dialog);
    if (parent)
        gtk_window_set_modal(GTK_WINDOW(parent), TRUE);
    if (response != GTK_RESPONSE_NONE)
        gtk_widget_destroy(GTK_WIDGET(dialog));

    return response;
}

/**
 * gwy_require_image_same_units:
 * @field: The data field to check.
 * @data: Data container with the data field.
 * @id: The id of @field in @data.
 * @name: Error message prefix, usually module function name.
 *
 * Checks if data field has equal lateral and value units, possibly showing error message.
 *
 * If the data field does not have the same lateral and value units a simple error message dialog is shown for @data
 * and @id.  This is only done if other GUI functions (like progress bars for waiting) are enabled.
 *
 * Returns: %TRUE if the units are the same, %FALSE otherwise.
 *
 * Since: 2.59
 **/
gboolean
gwy_require_image_same_units(GwyDataField *field, GwyContainer *data, gint id,
                             const gchar *name)
{
    GtkWidget *dialog;

    if (gwy_si_unit_equal(gwy_data_field_get_si_unit_xy(field), gwy_data_field_get_si_unit_z(field)))
        return TRUE;

    if (!gwy_app_data_browser_get_gui_enabled() && !gwy_app_wait_get_enabled())
        return FALSE;

    dialog = gtk_message_dialog_new(gwy_app_find_window_for_channel(data, id),
                                    GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_ERROR,
                                    GTK_BUTTONS_OK,
                                    _("%s: Lateral dimensions and value must be the same physical quantity."), name);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_NONE)
        gtk_widget_destroy(dialog);

    return FALSE;
}

/**
 * gwy_require_square_image:
 * @field: The data field to check.
 * @data: Data container with the data field.
 * @id: The id of @field in @data.
 * @name: Error message prefix, usually module function name.
 *
 * Checks if data field has the same horizontal and vertical pixel dimensions, possibly showing error message.
 *
 * If the data field is not square a simple error message dialog is shown for @data and @id.  This is only done if
 * other GUI functions (like progress bars for waiting) are enabled.
 *
 * Returns: %TRUE if the data field is square, %FALSE otherwise.
 *
 * Since: 2.59
 **/
gboolean
gwy_require_square_image(GwyDataField *field, GwyContainer *data, gint id,
                         const gchar *name)
{
    GtkWidget *dialog;

    if (gwy_data_field_get_xres(field) == gwy_data_field_get_yres(field))
        return TRUE;

    if (!gwy_app_data_browser_get_gui_enabled() && !gwy_app_wait_get_enabled())
        return FALSE;

    dialog = gtk_message_dialog_new(gwy_app_find_window_for_channel(data, id),
                                    GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_ERROR,
                                    GTK_BUTTONS_OK,
                                    _("%s: Data must be square."), name);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_NONE)
        gtk_widget_destroy(dialog);

    return FALSE;
}

static GwyPixmapLayer*
create_basic_layer(GwyDataView *dataview, gint id)
{
    GwyPixmapLayer *layer = gwy_layer_basic_new();
    GQuark quark = gwy_app_get_data_key_for_id(id);
    const gchar *key = g_quark_to_string(quark);
    gchar buf[24];

    gwy_pixmap_layer_set_data_key(layer, key);
    key = g_quark_to_string(gwy_app_get_data_palette_key_for_id(id));
    gwy_layer_basic_set_gradient_key(GWY_LAYER_BASIC(layer), key);
    key = g_quark_to_string(gwy_app_get_data_range_type_key_for_id(id));
    gwy_layer_basic_set_range_type_key(GWY_LAYER_BASIC(layer), key);
    g_snprintf(buf, sizeof(buf), "/%d/base", id);
    gwy_layer_basic_set_min_max_key(GWY_LAYER_BASIC(layer), buf);
    gwy_data_view_set_base_layer(dataview, layer);

    return layer;
}

static GwyPixmapLayer*
create_mask_layer(GwyDataView *dataview, gint id)
{
    GwyPixmapLayer *layer = gwy_layer_mask_new();
    GQuark quark = gwy_app_get_mask_key_for_id(id);
    const gchar *key = g_quark_to_string(quark);
    GwyContainer *data;
    GwyRGBA rgba;

    gwy_pixmap_layer_set_data_key(layer, key);
    gwy_layer_mask_set_color_key(GWY_LAYER_MASK(layer), key);
    gwy_data_view_set_alpha_layer(dataview, layer);

    data = gwy_data_view_get_data(dataview);
    if (!gwy_rgba_get_from_container(&rgba, data, key)) {
        gwy_rgba_get_from_container(&rgba, gwy_app_settings_get(), "/mask");
        gwy_rgba_store_to_container(&rgba, data, key);
    }

    return layer;
}

/**
 * gwy_create_preview:
 * @data: Data container with images to preview.
 * @id: Data id of preview image in @data.
 * @size: Preview size.  Prefrably one of the standard sizes.
 * @have_mask: %TRUE if the preview will have mask and should be set up so.
 *
 * Creates and sets up a preview widget for use in modules.
 *
 * Returns: A new data view widget with the preview.
 *
 * Since: 2.59
 **/
GtkWidget*
gwy_create_preview(GwyContainer *data,
                   gint id,
                   gint size,
                   gboolean have_mask)
{
    GQuark quark = gwy_app_get_data_key_for_id(id);
    const gchar *key = g_quark_to_string(quark);
    GwyDataView *dataview;
    GtkWidget *widget;

    widget = gwy_data_view_new(data);
    dataview = GWY_DATA_VIEW(widget);
    gwy_data_view_set_data_prefix(dataview, key);
    create_basic_layer(dataview, id);
    if (have_mask)
        create_mask_layer(dataview, id);
    gwy_set_data_preview_size(dataview, size);
    g_object_set_data(G_OBJECT(widget), "gwy-preview-size", GINT_TO_POINTER(size));

    return widget;
}

/**
 * gwy_create_dialog_preview_hbox:
 * @dialog: A data processing dialog.
 * @dataview: Preview data view, usually created by gwy_create_preview().
 * @pack_end: %TRUE to pack the data view to the end; %FALSE to pack it to the start.
 *
 * Creates the typical data processing dialog hbox with a preview.
 *
 * The box is added to the dialog.
 *
 * Returns: A new #GtkBox widget.
 *
 * Since: 2.59
 **/
GtkWidget*
gwy_create_dialog_preview_hbox(GtkDialog *dialog,
                               GwyDataView *dataview,
                               gboolean pack_end)
{
    GtkWidget *hbox, *alignment, *vbox;
    gint size;

    hbox = gwy_hbox_new(8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);

    g_return_val_if_fail(GTK_IS_DIALOG(dialog), hbox);
    g_return_val_if_fail(GWY_IS_DATA_VIEW(dataview), hbox);

    if (GWY_IS_DIALOG(dialog))
        gwy_dialog_add_content(GWY_DIALOG(dialog), hbox, FALSE, FALSE, 0);
    else {
        vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    }

    alignment = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    if ((size = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(dataview), "gwy-preview-size"))))
        gtk_widget_set_size_request(alignment, size, -1);
    gtk_container_add(GTK_CONTAINER(alignment), GTK_WIDGET(dataview));
    if (pack_end)
        gtk_box_pack_end(GTK_BOX(hbox), alignment, FALSE, FALSE, 0);
    else
        gtk_box_pack_start(GTK_BOX(hbox), alignment, FALSE, FALSE, 0);

    return hbox;
}

/**
 * gwy_create_preview_vector_layer:
 * @dataview: Preview data view, usually created by gwy_create_preview().
 * @id: Data id of preview image in @dataview's data.
 * @name: Layer/selection type name, without the GwyLayer/GwySelection prefix.  So usually "Point", "Line" or
 *        "Rectangle".
 * @max_objects: Maximum number of objects in the selection.
 * @editable: %TRUE for a user-editable selection, %FALSE to disable editing.
 *
 * Creates and sets up a vector layer for use in module previews.
 *
 * The selection's key will have the form "/0/select/lcname", where @lcname is the lowercase version of @name.  You
 * can query the layer using #GwyDataView functions.
 *
 * Parameters @max_objects and @editable exist for convenience.  The corresponding properties can be modified later.
 *
 * In typical scenarios this function is also suitable for switching vector layers in the preview.  Simply create
 * a different layer and the current one will be discarded – this assumes you do not keep the old layer object around.
 * Selections are kept in @dataview's data, so they will be automatically restored when switching back and forth.
 *
 * Returns: The layer's selection.  No reference is added; use g_object_ref() if you want to keep the selection
 *          around after the preview is destroyed.
 *
 * Since: 2.61
 **/
GwySelection*
gwy_create_preview_vector_layer(GwyDataView *dataview,
                                gint id,
                                const gchar *name,
                                gint max_objects,
                                gboolean editable)
{
    GwyVectorLayer *layer;
    GwySelection *selection;
    GType type;
    gchar *s, *t;

    s = g_strconcat("GwyLayer", name, NULL);
    type = g_type_from_name(s);
    g_free(s);
    g_return_val_if_fail(type, NULL);
    g_return_val_if_fail(g_type_is_a(type, GWY_TYPE_VECTOR_LAYER), NULL);

    layer = GWY_VECTOR_LAYER(g_object_new(type, NULL));
    s = g_strdup_printf("/%d/select/%s", id, name);
    t = strrchr(s, '/');
    g_assert(t);
    t++;
    *t = g_ascii_tolower(*t);
    gwy_vector_layer_set_selection_key(layer, s);
    g_free(s);
    gwy_vector_layer_set_editable(layer, editable);
    gwy_data_view_set_top_layer(dataview, layer);

    selection = gwy_vector_layer_ensure_selection(layer);
    gwy_selection_set_max_objects(selection, max_objects);

    return selection;
}

static void
active_page_changed(GtkNotebook *notebook, G_GNUC_UNUSED gpointer some_gtk_shit, gint pageno, GwyParams *params)
{
    gint id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(notebook), "gwy-active-page-param-id"));
    gwy_params_set_int(params, id, pageno);
}

static void
switch_active_page(GtkNotebook *notebook, GwyParams *params)
{
    gint id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(notebook), "gwy-active-page-param-id"));
    gtk_notebook_set_current_page(notebook, gwy_params_get_int(params, id));
}

/**
 * gwy_param_active_page_link_to_notebook:
 * @params: A parameter value set.
 * @id: Parameter identifier.
 * @notebook: A notebook widget.
 *
 * Connects an active page parameter to a notebook.
 *
 * This function should be called after the notebook was fully constructed.  The active notebook page is set according
 * to the parameter (clamping values that refer to non-existent pages).  Future page switches will update the
 * parameter value, allowing to simply save it to settings when the module dialog is finished.
 *
 * Since: 2.59
 **/
void
gwy_param_active_page_link_to_notebook(GwyParams *params,
                                       gint id,
                                       GtkNotebook *notebook)
{
    GwyParamDef *pardef;
    const GwyParamDefItem *def;
    gint pageno, npages;

    g_return_if_fail(GWY_IS_PARAMS(params));
    g_return_if_fail(GTK_IS_NOTEBOOK(notebook));
    g_return_if_fail(!g_object_get_data(G_OBJECT(notebook), "gwy-active-page-param-id"));

    pardef = gwy_params_get_def(params);
    def = _gwy_param_def_item(pardef, _gwy_param_def_index(pardef, id));
    g_return_if_fail(def && def->type == GWY_PARAM_ACTIVE_PAGE);

    npages = gtk_notebook_get_n_pages(notebook);
    pageno = gwy_params_get_int(params, id);
    pageno = CLAMP(pageno, 0, npages-1);
    gwy_params_set_int(params, id, pageno);

    /* Here we assume params will outlive notebook. */
    g_object_set_data(G_OBJECT(notebook), "gwy-active-page-param-id", GINT_TO_POINTER(id));
    /* NB: Switching pages before the notebook is shown has no effect.  See GtkNotebook docs. */
    g_signal_connect(notebook, "show", G_CALLBACK(switch_active_page), params);
    g_signal_connect(notebook, "switch-page", G_CALLBACK(active_page_changed), params);
}

static void
expander_expanded(GtkExpander *expander,
                  G_GNUC_UNUSED GParamSpec *pspec,
                  GwyParams *params)
{
    gint id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(expander), "gwy-expander-param-id"));

    gwy_params_set_boolean(params, id, gtk_expander_get_expanded(expander));
}

/**
 * gwy_create_expander_with_param:
 * @label: Expander label markup. Usually boldface Options.
 * @params: A parameter value set.
 * @id: Parameter identifier.
 *
 * Creates an expander linked to a boolean parameter.
 *
 * This is a convenience function for making an expander whose state is remembered in settings. The parameter
 * identified by @id must be a boolean parameter. The expander initial state will be set from the parameter and,
 * conversely, the parameter will be updated according to the expander state.
 *
 * Returns: A newly created #GtkExpander widget.
 *
 * Since: 2.62
 **/
GtkWidget*
gwy_create_expander_with_param(const gchar *label,
                               GwyParams *params,
                               gint id)
{
    GtkWidget *expander;

    expander = gtk_expander_new(label);
    gtk_expander_set_use_markup(GTK_EXPANDER(expander), TRUE);
    gtk_expander_set_expanded(GTK_EXPANDER(expander), gwy_params_get_boolean(params, id));
    g_object_set_data(G_OBJECT(expander), "gwy-expander-param-id", GINT_TO_POINTER(id));
    g_signal_connect(expander, "notify::expanded", G_CALLBACK(expander_expanded), params);

    return expander;
}

static void
graph_range_param_changed(GraphRangeData *grdata, gint id)
{
    GwyParams *params;
    gdouble range[2];

    if (grdata->in_update || (id != grdata->id_from && id != grdata->id_to))
        return;

    params = gwy_param_table_params(grdata->table);
    range[0] = gwy_params_get_double(params, grdata->id_from);
    range[1] = gwy_params_get_double(params, grdata->id_to);
    if (range[0] <= grdata->min && range[1] >= grdata->max)
        gwy_selection_clear(grdata->selection);
    else
        gwy_selection_set_object(grdata->selection, 0, range);
}

static void
graph_range_selection_changed(GwySelection* selection, gint i, GraphRangeData *grdata)
{
    gdouble range[2];
    gdouble xfrom, xto;
    gboolean have_range = TRUE;

    g_return_if_fail(i <= 0);

    if (gwy_selection_get_data(selection, NULL) <= 0)
        have_range = FALSE;
    else {
        gwy_selection_get_object(selection, 0, range);
        if (range[0] == range[1])
            have_range = FALSE;
    }
    if (have_range) {
        xfrom = MIN(range[0], range[1]);
        xto = MAX(range[0], range[1]);
    }
    else {
        xfrom = grdata->min;
        xto = grdata->max;
    }

    _gwy_param_table_in_update(grdata->table, TRUE);
    gwy_param_table_set_double(grdata->table, grdata->id_from, xfrom);
    gwy_param_table_set_double(grdata->table, grdata->id_to, xto);
    _gwy_param_table_in_update(grdata->table, FALSE);

    grdata->in_update = TRUE;
    gwy_param_table_param_changed(grdata->table, grdata->id_from);
    gwy_param_table_param_changed(grdata->table, grdata->id_to);
    grdata->in_update = FALSE;
}

/**
 * gwy_create_graph_xrange_with_params:
 * @partable: Set of parameter value controls.
 * @id_from: Parameter identifier for the beginning.
 * @id_to: Parameter identifier for the end.
 * @graph: Graph widget where the @x-range will be selected.
 * @gmodel: Graph model used for determining the @x range. May be %NULL to use @graph's model, although this is not
 *          usually useful.
 *
 * Sets up a parameter table range row for horizontal graph selection.
 *
 * A row with the two parameters is added to the table. The graph is set up for %GWY_GRAPH_STATUS_XSEL.
 * Synchronisation between the parameters and selection is set up.
 *
 * The caller still needs to run gwy_dialog_invalidate() or use a similar mechanism when the parameter changes.
 * Usually, any change to the selection will result in two updates, one for each parameter.
 *
 * Since: 2.63
 **/
void
gwy_create_graph_xrange_with_params(GwyParamTable *partable,
                                    gint id_from,
                                    gint id_to,
                                    GwyGraph *graph,
                                    GwyGraphModel *gmodel)
{
    GraphRangeData *grdata;
    GwyParams *params;
    GwySIValueFormat *vf;
    GwySelection *selection;
    GwySIUnit *unit;
    gdouble min, max, range;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    g_return_if_fail(GWY_IS_GRAPH(graph));

    params = gwy_param_table_params(partable);
    gmodel = (gmodel ? gmodel : gwy_graph_get_model(graph));
    g_return_if_fail(GWY_IS_GRAPH_MODEL(gmodel));

    gwy_graph_model_get_x_range(gmodel, &min, &max);
    gwy_params_set_double(params, id_from, min);
    gwy_params_set_double(params, id_to, max);

    g_object_get(gmodel, "si-unit-x", &unit, NULL);
    range = MAX(fabs(min), fabs(max));
    vf = gwy_si_unit_get_format_with_digits(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, range, 3, NULL);

    gwy_graph_set_status(GWY_GRAPH(graph), GWY_GRAPH_STATUS_XSEL);
    selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(graph))),
                                             GWY_GRAPH_STATUS_XSEL);
    gwy_selection_set_max_objects(selection, 1);

    gwy_param_table_append_range(partable, id_from, id_to);
    gwy_param_table_entry_set_value_format(partable, id_from, vf);

    g_object_unref(unit);
    gwy_si_unit_value_format_free(vf);

    grdata = g_new0(GraphRangeData, 1);
    grdata->min = min;
    grdata->max = max;
    grdata->selection = selection;
    grdata->table = partable;
    grdata->id_from = id_from;
    grdata->id_to = id_to;
    g_object_weak_ref(G_OBJECT(selection), graph_range_selection_destroyed, grdata);
    g_object_weak_ref(G_OBJECT(partable), graph_range_table_destroyed, grdata);

    grdata->param_id = g_signal_connect_swapped(partable, "param-changed",
                                                G_CALLBACK(graph_range_param_changed), grdata);
    grdata->selection_id = g_signal_connect(selection, "changed", G_CALLBACK(graph_range_selection_changed), grdata);
}

static void
graph_range_selection_destroyed(gpointer user_data, G_GNUC_UNUSED GObject *selection)
{
    GraphRangeData *grdata = (GraphRangeData*)user_data;

    /* Selection has been destroyed; cross-destroy anything table-related. */
    g_signal_handler_disconnect(grdata->table, grdata->param_id);
    g_object_weak_unref(G_OBJECT(grdata->table), graph_range_table_destroyed, grdata);
    gwy_clear(grdata, 1);
    g_free(grdata);
}

static void
graph_range_table_destroyed(gpointer user_data, G_GNUC_UNUSED GObject *partable)
{
    GraphRangeData *grdata = (GraphRangeData*)user_data;

    /* Param table has been destroyed; cross-destroy anything selection-related. */
    g_signal_handler_disconnect(grdata->selection, grdata->selection_id);
    g_object_weak_unref(G_OBJECT(grdata->selection), graph_range_selection_destroyed, grdata);
    gwy_clear(grdata, 1);
    g_free(grdata);
}

static gboolean
item_is_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer default_item)
{
    gpointer item;

    gtk_tree_model_get(model, iter, 0, &item, -1);
    return item != default_item;
}

/**
 * gwy_create_inventory_model_without_default:
 * @store: An inventory store.
 *
 * Creates a tree model filter around an inventory store which hides the default item.
 *
 * The model can be used to create resource lists without the default item if it is special and should not be shown to
 * users. Remember you need to translate iters between the models using
 * gtk_tree_model_filter_convert_child_iter_to_iter() and gtk_tree_model_filter_convert_iter_to_child_iter().
 *
 * Returns: A newly created #GtkTreeModelFilter wrapping @store.
 *
 * Since: 2.62
 **/
GtkTreeModel*
gwy_create_inventory_model_without_default(GwyInventoryStore *store)
{
    GtkTreeModel *filtermodel;
    gpointer default_item;

    g_return_val_if_fail(GWY_IS_INVENTORY_STORE(store), NULL);
    default_item = gwy_inventory_get_default_item(gwy_inventory_store_get_inventory(store));
    filtermodel = gtk_tree_model_filter_new(GTK_TREE_MODEL(store), NULL);
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filtermodel), &item_is_visible, default_item, NULL);

    return filtermodel;
}

/**
 * gwy_select_in_filtered_inventory_treeeview:
 * @treeview: A filtered tree view showing an inventory.
 * @name: Item name to select.
 *
 * Selects an item in filtered tree view showing an inventory.
 *
 * Usually the tree view would show a #GtkTreeModelFilter model created by
 * gwy_create_inventory_model_without_default(). However, this function works also for other filter models around
 * a #GwyInventoryStore.
 *
 * Since: 2.62
 **/
void
gwy_select_in_filtered_inventory_treeeview(GtkTreeView *treeview,
                                           const gchar *name)
{
    GtkTreeModelFilter *filtermodel;
    GwyInventoryStore *store;
    GtkTreeIter storeiter, filteriter;
    GtkTreeSelection *selection;

    filtermodel = GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(treeview));
    store = GWY_INVENTORY_STORE(gtk_tree_model_filter_get_model(filtermodel));
    if (gwy_inventory_store_get_iter(store, name, &storeiter)
        && gtk_tree_model_filter_convert_child_iter_to_iter(filtermodel, &filteriter, &storeiter)) {
        selection = gtk_tree_view_get_selection(treeview);
        gtk_tree_selection_select_iter(selection, &filteriter);
    }
}

/**
 * gwy_set_gradient_for_residuals:
 * @gradient: A false colour gradient to modify.
 * @fullmin: Minimum residual value (presumably negative).
 * @fullmax: Maximum residual value (presumably positive).
 * @dispmin: Natural mapping minimum. It will be set to the fixed mapping minimum to actually use.
 * @dispmax: Natural mapping maximum. It will be set to the fixed mapping maximum to actually use.
 *
 * Sets a false colour gradient suitably for fit residuals.
 *
 * If the provided @dispmin and @dispmax are set to the mapping minimum and maximum, the resulting gradient will have
 * neutral light colour for zero, a red gradient for postive values and a blue gradient for negative values. An effort
 * is made to do something reasonable even when all values are positive or negative.
 *
 * Usually @fullmin and @fullmax are simply the minimun and maximum, whereas @dispmin and @dispmax come from
 * a function like gwy_data_field_get_autorange(). Altough the full range might also be used.
 *
 * Since: 2.64
 **/
void
gwy_set_gradient_for_residuals(GwyGradient *gradient,
                               gdouble fullmin, gdouble fullmax,
                               gdouble *dispmin, gdouble *dispmax)
{
    static const GwyRGBA rgba_negative = { 0.0, 0.0, 1.0, 1.0 };
    static const GwyRGBA rgba_positive = { 1.0, 0.0, 0.0, 1.0 };
    static const GwyRGBA rgba_neutral = { 1.0, 1.0, 1.0, 1.0 };

    GwyGradientPoint zero_pt = { 0.5, rgba_neutral };
    gdouble zero, range;
    GwyRGBA rgba;
    gint pos;

    g_return_if_fail(GWY_IS_GRADIENT(gradient));
    g_return_if_fail(dispmin && dispmax);

    gwy_gradient_reset(gradient);
    fullmin = fmin(fullmin, *dispmin);
    fullmax = fmax(fullmax, *dispmax);

    /* Stretch the scale to the range when all the data are too high or too low. */
    if (*dispmin >= 0.0) {
        gwy_gradient_set_point_color(gradient, 0, &rgba_neutral);
        gwy_gradient_set_point_color(gradient, 1, &rgba_positive);
        return;
    }
    if (*dispmax <= 0.0) {
        gwy_gradient_set_point_color(gradient, 0, &rgba_negative);
        gwy_gradient_set_point_color(gradient, 1, &rgba_neutral);
        return;
    }
    if (!(*dispmax > *dispmin)) {
        gwy_gradient_set_point_color(gradient, 0, &rgba_negative);
        gwy_gradient_set_point_color(gradient, 1, &rgba_positive);
        pos = gwy_gradient_insert_point_sorted(gradient, &zero_pt);
        g_assert(pos == 1);
        return;
    }

    /* Otherwise make zero neutral and map the two colours to both side, with the same scale. */
    range = fmax(-*dispmin, *dispmax);
    *dispmin = -range;
    *dispmax = range;

    if (-fullmin >= range && fullmax >= range) {
        /* Symmetrically extended display range lies within the full data range.  Use this as the display range with
         * fully extended colour gradients. */
        gwy_gradient_set_point_color(gradient, 0, &rgba_negative);
        gwy_gradient_set_point_color(gradient, 1, &rgba_positive);
    }
    else {
        if (fullmax < range) {
            /* Map [-range, fullmax] to colours [negative, cut_positive]. */
            zero = range/(fullmax + range);
            *dispmax = fullmax;
            gwy_gradient_set_point_color(gradient, 0, &rgba_negative);
            gwy_rgba_interpolate(&rgba_neutral, &rgba_positive, (1.0 - zero)/zero, &rgba);
            gwy_gradient_set_point_color(gradient, 1, &rgba);
        }
        else {
            /* Map [fullmin, range] to colours [cut_negative, positive]. */
            zero = -fullmin/(range - fullmin);
            *dispmin = fullmin;
            gwy_rgba_interpolate(&rgba_neutral, &rgba_negative, zero/(1.0 - zero), &rgba);
            gwy_gradient_set_point_color(gradient, 0, &rgba);
            gwy_gradient_set_point_color(gradient, 1, &rgba_positive);
        }
        zero_pt.x = zero;
    }

    pos = gwy_gradient_insert_point_sorted(gradient, &zero_pt);
    g_assert(pos == 1);
}

/************************** Documentation ****************************/

/**
 * SECTION:gwymoduleutils
 * @title: module utils
 * @short_description: Module utility functions
 * @include: app/gwymoduleutils.h
 **/

/**
 * GwySaveAuxiliaryCreate:
 * @user_data: The data passed to gwy_save_auxiliary_with_callback() as @user_data.
 * @data_len: The length of the returned data in bytes.  Leaving it unset has the same effect as setting it to
 *            a negative value.  See gwy_save_auxiliary_data() for details.
 *
 * The type of auxiliary saved data creation function.
 *
 * Returns: The data to save.  It must not return %NULL.
 *
 * Since: 2.3
 **/

/**
 * GwySaveAuxiliaryDestroy:
 * @data: The data returned by the corresponding #GwySaveAuxiliaryCreate function.
 * @user_data: The data passed to gwy_save_auxiliary_with_callback() as @user_data.
 *
 * The type of auxiliary saved data destruction function.
 *
 * Since: 2.3
 **/

/**
 * GwyPreviewSurfaceFlags:
 * @GWY_PREVIEW_SURFACE_DENSITY: Render a point density map instead of the data.
 * @GWY_PREVIEW_SURFACE_FILL: Make the data field as large as the specified resolutions at least in one dimension (it
 *                            can be prevented in the other by different aspect ratio).
 *
 * Type of behaviour modifying flags that can be passed to
 * gwy_preview_surface_to_datafield().
 *
 * Since: 2.46
 **/

/* vim: set cin et columns=120 tw=118 ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
