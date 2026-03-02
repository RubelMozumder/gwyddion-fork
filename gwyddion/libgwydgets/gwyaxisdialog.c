/*
 *  $Id: gwyaxisdialog.c 20678 2017-12-18 18:26:55Z yeti-dn $
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwyscitext.h>
#include <libgwydgets/gwyaxisdialog.h>

static gboolean gwy_axis_dialog_delete(GtkWidget *widget,
                                       GdkEventAny *event);

G_DEFINE_TYPE(GwyAxisDialog, _gwy_axis_dialog, GTK_TYPE_DIALOG)

static void
_gwy_axis_dialog_class_init(GwyAxisDialogClass *klass)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    widget_class->delete_event = gwy_axis_dialog_delete;
}

static void
_gwy_axis_dialog_init(G_GNUC_UNUSED GwyAxisDialog *dialog)
{
}

static gboolean
gwy_axis_dialog_delete(GtkWidget *widget,
                       G_GNUC_UNUSED GdkEventAny *event)
{
    gtk_widget_hide(widget);

    return TRUE;
}

/**
 * _gwy_axis_dialog_new:
 * @axis: The axis to create dialog for,
 *
 * Creates a new axis dialog.
 *
 * Returns: A new axis dialog as a #GtkWidget.
 **/
GtkWidget*
_gwy_axis_dialog_new(GwyAxis *axis)
{
    GwyAxisDialog *dialog;
    GtkWidget *label, *table;
    gint row;

    dialog = GWY_AXIS_DIALOG(g_object_new(GWY_TYPE_AXIS_DIALOG, NULL));
    dialog->axis = axis;

    gtk_window_set_title(GTK_WINDOW(dialog), _("Axis Properties"));

    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CLOSE);

    table = gtk_table_new(2, 4, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), table);
    row = 0;

    label = gwy_label_new_header(_("Label Text"));
    gtk_table_attach(GTK_TABLE(table), label,
                     0, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    dialog->sci_text = gwy_sci_text_new();
    gtk_table_attach(GTK_TABLE(table), dialog->sci_text,
                     0, 4, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    gtk_widget_show_all(table);

    return GTK_WIDGET(dialog);
}

GtkWidget*
_gwy_axis_dialog_get_sci_text(GtkWidget* dialog)
{
    return GWY_AXIS_DIALOG(dialog)->sci_text;
}

/*
 * SECTION:gwyaxisdialog
 * @title: GwyAxisDialog
 * @short_description: Axis properties dialog
 *
 * #GwyAxisDialog is used for setting the text properties
 * of the axis. It is used namely with #GwyAxis.
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
