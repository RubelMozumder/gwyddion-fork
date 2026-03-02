/*
 *  $Id: gwycheckboxes.c 21106 2018-05-27 10:20:06Z yeti-dn $
 *  Copyright (C) 2003-2018 David Necas (Yeti), Petr Klapetek.
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
#include <stdarg.h>
#include <gtk/gtkcheckbutton.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwydgets/gwycheckboxes.h>

typedef struct {
    GSList *group;
    gulong handler_id;
    guint value;
    gboolean change_me : 1;
    gboolean change_me_to : 1;
} GwyCheckBoxData;

static GQuark gwycb_quark = 0;

static void
setup_quark(void)
{
    if (!gwycb_quark)
        gwycb_quark = g_quark_from_static_string("gwy-checkboxes-key");
}

/* Remove self from the group and update the group on all other members. */
static void
check_box_destroyed(GObject *checkbox)
{
    GwyCheckBoxData *data, *data2;
    GSList *group, *newgroup, *l, *removeme;

    data = g_object_get_qdata(checkbox, gwycb_quark);
    g_return_if_fail(data);
    group = data->group;
    g_return_if_fail(group);

    removeme = g_slist_find(group, checkbox);
    g_return_if_fail(removeme);

    newgroup = g_slist_remove_link(group, removeme);

    if (newgroup != group) {
        for (l = newgroup; l; l = g_slist_next(l)) {
            data2 = g_object_get_qdata(G_OBJECT(l->data), gwycb_quark);
            data2->group = newgroup;
        }
    }

    /* Do not actually free anything before we the group in all other check
     * boxes. */
    g_slist_free_1(removeme);
    g_slice_free(GwyCheckBoxData, data);
}

static GSList*
gwy_check_boxes_create_real(const GwyEnum *entries,
                            gint nentries,
                            GCallback callback,
                            gpointer cbdata,
                            gint selected,
                            gboolean translate)
{
    GwyCheckBoxData *data;
    GtkWidget *button;
    GSList *group = NULL, *l;
    const gchar *name;
    gint i;

    if (nentries < 0) {
        for (nentries = 0; entries[nentries].name != NULL; nentries++)
            ;
    }

    setup_quark();
    for (i = nentries-1; i >= 0; i--) {
        name = translate ? gwy_sgettext(entries[i].name) : entries[i].name;
        button = gtk_check_button_new_with_mnemonic(name);
        data = g_slice_new0(GwyCheckBoxData);
        data->value = entries[i].value;
        g_object_set_qdata(G_OBJECT(button), gwycb_quark, data);
        if (entries[i].value & selected)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
        group = g_slist_prepend(group, button);
    }

    for (l = group; l; l = g_slist_next(l)) {
        data = g_object_get_qdata(G_OBJECT(l->data), gwycb_quark);
        data->group = group;
        g_signal_connect(G_OBJECT(l->data), "destroy",
                         G_CALLBACK(check_box_destroyed), NULL);
        if (callback) {
            data->handler_id = g_signal_connect(l->data, "toggled",
                                                callback, cbdata);
        }
    }

    return group;
}

/**
 * gwy_check_boxes_create:
 * @entries: Radio button group items.
 * @nentries: The number of items.  Negative value means that @entries is
 *            terminated with a %NULL-named item.
 * @callback: A callback called when a button is selected (or %NULL for no
 *            callback).
 * @cbdata: User data passed to the callback.
 * @selected: Combination of flags corresponding to selected items.
 *
 * Creates a check box group from a set of flags.
 *
 * All the enum values must be distinct flags, i.e. positive integers with
 * non-overlapping bits (binary AND of any two values must be zero).
 *
 * The group #GSList returned by this function is analogous to #GtkRadioButton
 * groups.  However, GTK+ does not know anything about it and it cannot be
 * used with any functions expecting a #GtkRadioButton group.
 *
 * Returns: The newly created check box group (a #GSList).  Iterate over
 *          the list and pack the widgets (the order is the same as in
 *          @entries).  The group is owned by the buttons and must not be
 *          freed.
 *
 * Since: 2.51
 **/
GSList*
gwy_check_boxes_create(const GwyEnum *entries,
                       gint nentries,
                       GCallback callback,
                       gpointer cbdata,
                       guint selected)
{
    return gwy_check_boxes_create_real(entries, nentries,
                                       callback, cbdata, selected, TRUE);
}

/**
 * gwy_check_boxes_createl:
 * @callback: A callback called when a button is selected (or %NULL for no
 *            callback).
 * @cbdata: User data passed to the callback.
 * @selected: Combination of flags corresponding to selected items.
 * @...: First item label, first item value, second item label, second item
 *       value, etc.  Terminated with %NULL.
 *
 * Creates a check box group from a list of label/value pairs.
 *
 * All the enum values must be distinct flags, i.e. positive integers with
 * non-overlapping bits (binary AND of any two values must be zero).
 *
 * The group #GSList returned by this function is analogous to #GtkRadioButton
 * groups.  However, GTK+ does not know anything about it and it cannot be
 * used with any functions expecting a #GtkRadioButton group.
 *
 * Returns: The newly created check box group (a #GSList).  Iterate over
 *          the list and pack the widgets (the order is the same as in
 *          @entries).  The group is owned by the buttons and must not be
 *          freed.
 *
 * Since: 2.51
 **/
GSList*
gwy_check_boxes_createl(GCallback callback,
                        gpointer cbdata,
                        guint selected,
                        ...)
{
    GSList *group;
    GwyEnum *entries;
    gint i, nentries;
    va_list ap;

    va_start(ap, selected);
    nentries = 0;
    while (va_arg(ap, const gchar*)) {
        (void)va_arg(ap, guint);
        nentries++;
    }
    va_end(ap);

    entries = g_new(GwyEnum, nentries);

    va_start(ap, selected);
    for (i = 0; i < nentries; i++) {
        entries[i].name = va_arg(ap, const gchar*);
        entries[i].value = va_arg(ap, gint);
    }
    va_end(ap);

    group = gwy_check_boxes_create_real(entries, nentries,
                                        callback, cbdata, selected, FALSE);
    g_free(entries);

    return group;
}

/**
 * gwy_check_boxes_attach_to_table:
 * @group: A check box group created by gwy_check_boxes_create() or
 *         gwy_check_boxes_createl().
 * @table: A table.
 * @colspan: The number of columns the check boxes should span across.
 * @row: Table row to start attaching at.
 *
 * Attaches a group of check boxes to table rows.
 *
 * Returns: The row after the last attached check box.
 *
 * Since: 2.51
 **/
gint
gwy_check_boxes_attach_to_table(GSList *group,
                                GtkTable *table,
                                gint colspan,
                                gint row)
{
    g_return_val_if_fail(GTK_IS_TABLE(table), row);

    while (group) {
        gtk_table_attach(table, GTK_WIDGET(group->data),
                         0, colspan, row, row + 1,
                         GTK_FILL, 0, 0, 0);
        row++;
        group = g_slist_next(group);
    }

    return row;
}

/**
 * gwy_check_boxes_set_selected:
 * @group: A check box group created by gwy_check_boxes_create().
 * @selected: Flags to be shown as currently selected.
 *
 * Sets the state of all check boxes in @group to given flag combination.
 *
 * If @selected differs significantly from the currently selected flags, lots
 * of check buttons will change state, resulting in lots of callbacks.  You
 * might want to avoid reacting to each invidivually.
 *
 * The callback passed upon construction will be called only after all the
 * state of all check boxes is updated so it will see the check boxes already
 * correspond to @selected.  However, any additional signal handlers you set up
 * will be called during the update unless you block them yourself.
 *
 * Since: 2.51
 **/
void
gwy_check_boxes_set_selected(GSList *group,
                             guint selected)
{
    GwyCheckBoxData *data;
    gboolean active, want_active,
             anything_to_do = FALSE, have_callbacks = FALSE;
    guint signo;
    GSList *l;

    setup_quark();
    /* Figure out what to do, if anything. */
    for (l = group; l; l = g_slist_next(l)) {
        data = g_object_get_qdata(G_OBJECT(l->data), gwycb_quark);
        g_return_if_fail(data);
        want_active = !!(selected & data->value);
        active = !!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(l->data));
        if (active != want_active) {
            data->change_me = TRUE;
            data->change_me_to = want_active;
            anything_to_do = TRUE;
        }
        else
            data->change_me = FALSE;
    }

    if (!anything_to_do)
        return;

    /* Change states. */
    for (l = group; l; l = g_slist_next(l)) {
        data = g_object_get_qdata(G_OBJECT(l->data), gwycb_quark);
        if (data->change_me) {
            if (data->handler_id) {
                have_callbacks = TRUE;
                g_signal_handler_block(l->data, data->handler_id);
            }
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(l->data),
                                         data->change_me_to);
        }
    }

    if (!have_callbacks)
        return;

    /* Emit signals when all is finished. */
    signo = g_signal_lookup("toggled", GTK_TYPE_TOGGLE_BUTTON);
    for (l = group; l; l = g_slist_next(l)) {
        data = g_object_get_qdata(G_OBJECT(l->data), gwycb_quark);
        if (data->change_me) {
            g_signal_handler_unblock(l->data, data->handler_id);
            g_signal_emit(l->data, signo, 0, NULL);
        }
    }
}

/**
 * gwy_check_boxes_get_selected:
 * @group: A check box group created by gwy_check_boxes_create() or
 *         gwy_check_boxes_createl().
 *
 * Gets the flag combination corresponding to currently selected items.
 *
 * Returns: The combination of flags corresponding to currently selected items.
 *
 * Since: 2.51
 **/
guint
gwy_check_boxes_get_selected(GSList *group)
{
    GwyCheckBoxData *data;
    GSList *l;
    guint selected = 0;

    setup_quark();
    for (l = group; l; l = g_slist_next(l)) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(l->data))) {
            data = g_object_get_qdata(G_OBJECT(l->data), gwycb_quark);
            g_return_val_if_fail(data, 0);
            selected |= data->value;
        }
    }

    return selected;
}

/**
 * gwy_check_boxes_find:
 * @group: A check box group created by gwy_check_boxes_create() or
 *         gwy_check_boxes_createl().
 * @value: The value associated with the check box to find.
 *
 * Finds a check box by its associated flag value.
 *
 * The value must corresponding exactly to the single flag.  Otherwise the
 * check box is not considered a match.
 *
 * Returns: The check box corresponding to @value, or %NULL on failure.
 *
 * Since: 2.51
 **/
GtkWidget*
gwy_check_boxes_find(GSList *group, guint value)
{
    GwyCheckBoxData *data;
    GSList *l;

    setup_quark();
    for (l = group; l; l = g_slist_next(l)) {
        data = g_object_get_qdata(G_OBJECT(l->data), gwycb_quark);
        g_return_val_if_fail(data, NULL);
        if (data->value == value)
            return GTK_WIDGET(l->data);
    }

    return NULL;
}

/**
 * gwy_check_boxes_set_sensitive:
 * @group: A check box group created by gwy_check_boxes_create().
 * @sensitive: %TRUE to make all choices sensitive, %FALSE to make all
 *             insensitive.
 *
 * Sets the sensitivity of all check box in a group.
 *
 * This function is useful to make the choice as a whole available/unavailable.
 * Use gwy_check_boxes_find() combined with gtk_widget_set_sensitive() to
 * manage sensitivity of individual options.
 *
 * Since: 2.51
 **/
void
gwy_check_boxes_set_sensitive(GSList *group,
                              gboolean sensitive)
{
    while (group) {
        g_return_if_fail(GTK_IS_CHECK_BUTTON(group->data));
        gtk_widget_set_sensitive(GTK_WIDGET(group->data), sensitive);
        group = g_slist_next(group);
    }
}

/**
 * gwy_check_box_get_value:
 * @checkbox: A check box belonging to a group created by
 *            gwy_check_boxes_create() or gwy_check_boxes_createl().
 *
 * Gets the flag value associated with a check box.
 *
 * Returns: The flag value corresponding to @button.
 *
 * Since: 2.51
 **/
guint
gwy_check_box_get_value(GtkWidget *checkbox)
{
    GwyCheckBoxData *data;

    g_return_val_if_fail(GTK_IS_CHECK_BUTTON(checkbox), 0);
    data = g_object_get_qdata(G_OBJECT(checkbox), gwycb_quark);
    g_return_val_if_fail(data, 0);
    return data->value;
}

/**
 * gwy_check_box_get_group:
 * @checkbox: A check box belonging to a group created by
 *            gwy_check_boxes_create() or gwy_check_boxes_createl().
 *
 * Gets the group a check box belongs to.
 *
 * Returns: The group @checkbox belongs to.
 *
 * Since: 2.51
 **/
GSList*
gwy_check_box_get_group(GtkWidget *checkbox)
{
    GwyCheckBoxData *data;

    g_return_val_if_fail(GTK_IS_CHECK_BUTTON(checkbox), 0);
    data = g_object_get_qdata(G_OBJECT(checkbox), gwycb_quark);
    g_return_val_if_fail(data, NULL);
    return data->group;
}

/************************** Documentation ****************************/

/**
 * SECTION:gwycheckboxes
 * @title: gwycheckboxes
 * @short_description: Check box group constructors for flags
 * @see_also: <link linkend="libgwydgets-gwycombobox">gwycombobox</link>
 *            -- combo box constructors;
 *            <link linkend="libgwydgets-gwyradiobuttons">gwyradiobuttons</link>
 *            -- radio button constructors
 *
 * Groups of check boxes associated with integer flags can be easily
 * constructed from #GwyEnum's with gwy_check_boxes_create().
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
