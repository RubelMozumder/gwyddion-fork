/*
 *  $Id: graph_export_bitmap.c 25588 2023-08-01 09:39:50Z yeti-dn $
 *  Copyright (C) 2006-2023 David Necas (Yeti), Petr Klapetek.
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-graph.h>
#include <app/gwyapp.h>

static gboolean module_register(void);
static void     export         (GwyGraph *graph);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Export graph into bitmap"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.1",
    "David Nečas (Yeti) & Petr Klapetek",
    "2006",
};

GWY_MODULE_QUERY2(module_info, graph_export_bitmap)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_export_bitmap",
                            (GwyGraphFunc)&export,
                            N_("/_Export/_Bitmap"),
                            GWY_STOCK_GRAPH_EXPORT_PNG,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Export graph to a raster image"));

    return TRUE;
}

static void
export(GwyGraph *graph)
{
    GtkWidget *dialog;
    GdkPixbuf *pixbuf;
    gchar *filename = NULL;
    GError *error = NULL;
    const gchar *ext, *format = "png";

    dialog = gtk_file_chooser_dialog_new(_("Export to Image"), NULL, GTK_FILE_CHOOSER_ACTION_SAVE,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_SAVE, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), gwy_app_get_current_directory());

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK && gwy_app_file_confirm_overwrite(dialog)) {
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        pixbuf = gwy_graph_export_pixmap(graph, TRUE, TRUE, TRUE);
        if ((ext = strrchr(filename, '.'))) {
            ext++;
            if (gwy_stramong(ext, "jpeg", "jpg", "jpe", NULL))
                format = "jpeg";
            else if (gwy_stramong(ext, "tiff", "tif", NULL))
                format = "tiff";
            else if (gwy_stramong(ext, "bmp", NULL))
                format = "bmp";
        }
        gdk_pixbuf_save(pixbuf, filename, format, &error, NULL);
        g_object_unref(pixbuf);
    }
    gtk_widget_destroy(dialog);

    if (error) {
        dialog = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                        _("Saving of `%s' failed"), filename);
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                                 _("Cannot write to file: %s."), error->message);
        gtk_widget_show_all(dialog);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_clear_error(&error);
    }
    g_free(filename);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
