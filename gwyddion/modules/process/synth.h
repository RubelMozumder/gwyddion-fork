/*
 *  $Id: synth.h 23811 2021-06-08 12:12:43Z yeti-dn $
 *  Copyright (C) 2009-2021 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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

#ifndef __GWY_SYNTH_H__
#define __GWY_SYNTH_H__ 1

#ifndef GWY_SYNTH_CONTROLS
#error GWY_SYNTH_CONTROLS must be defined to the GUI controls structure name
#endif
/* GWY_SYNTH_CONTROLS must have the following fields defined:
 * - table
 * - dims
 * - pxsize
 */

#ifndef GWY_SYNTH_INVALIDATE
#error GWY_SYNTH_INVALIDATE must be defined to the common invalidation code
#endif

#include <libgwydgets/gwydgetutils.h>

typedef void (*GwySynthUpdateValueFunc)(GWY_SYNTH_CONTROLS *controls);

static void          gwy_synth_boolean_changed       (GWY_SYNTH_CONTROLS *controls,
                                                      GtkToggleButton *toggle);
static void          gwy_synth_boolean_changed_silent(GtkToggleButton *button,
                                                      gboolean *target);
static void          gwy_synth_toggle_sensitive      (GtkToggleButton *toggle,
                                                      GtkWidget *widget);
static void          gwy_synth_double_changed        (GWY_SYNTH_CONTROLS *controls,
                                                      GtkAdjustment *adj);
static void          gwy_synth_int_changed           (GWY_SYNTH_CONTROLS *controls,
                                                      GtkAdjustment *adj);
static GtkWidget*    gwy_synth_instant_updates_new   (GWY_SYNTH_CONTROLS *controls,
                                                      GtkWidget **pupdate,
                                                      GtkWidget **pinstant,
                                                      gboolean *target);
static void          gwy_synth_randomize_seed        (GtkAdjustment *adj);
static GtkWidget*    gwy_synth_random_seed_new       (GWY_SYNTH_CONTROLS *controls,
                                                      GtkObject **adj,
                                                      gint *target);
static GtkWidget*    gwy_synth_randomize_new         (gboolean *target);

G_GNUC_UNUSED
static void
gwy_synth_boolean_changed(GWY_SYNTH_CONTROLS *controls,
                          GtkToggleButton *toggle)
{
    GObject *object = G_OBJECT(toggle);
    gboolean *target = g_object_get_data(object, "target");

    g_return_if_fail(target);
    *target = gtk_toggle_button_get_active(toggle);
    GWY_SYNTH_INVALIDATE(controls);
}

G_GNUC_UNUSED
static void
gwy_synth_boolean_changed_silent(GtkToggleButton *button,
                                 gboolean *target)
{
    *target = gtk_toggle_button_get_active(button);
}

G_GNUC_UNUSED
static void
gwy_synth_toggle_sensitive(GtkToggleButton *toggle,
                           GtkWidget *widget)
{
    gtk_widget_set_sensitive(widget, !gtk_toggle_button_get_active(toggle));
}

G_GNUC_UNUSED
static void
gwy_synth_int_changed(GWY_SYNTH_CONTROLS *controls,
                      GtkAdjustment *adj)
{
    GObject *object = G_OBJECT(adj);
    gint *target = g_object_get_data(object, "target");

    g_return_if_fail(target);
    *target = gwy_adjustment_get_int(adj);
    GWY_SYNTH_INVALIDATE(controls);
}

G_GNUC_UNUSED
static void
gwy_synth_double_changed(GWY_SYNTH_CONTROLS *controls,
                         GtkAdjustment *adj)
{
    GObject *object = G_OBJECT(adj);
    gdouble *target = g_object_get_data(object, "target");
    GwySynthUpdateValueFunc update_value = g_object_get_data(object,
                                                             "update-value");

    g_return_if_fail(target);
    *target = gtk_adjustment_get_value(adj);
    if (update_value)
        update_value(controls);
    GWY_SYNTH_INVALIDATE(controls);
}

G_GNUC_UNUSED
static GtkWidget*
gwy_synth_instant_updates_new(GWY_SYNTH_CONTROLS *controls,
                              GtkWidget **pupdate,
                              GtkWidget **pinstant,
                              gboolean *target)
{
    GtkWidget *hbox;

    hbox = gtk_hbox_new(FALSE, 6);

    *pupdate = gtk_button_new_with_mnemonic(_("_Update"));
    gtk_widget_set_sensitive(*pupdate, !*target);
    gtk_box_pack_start(GTK_BOX(hbox), *pupdate, FALSE, FALSE, 0);

    *pinstant = gtk_check_button_new_with_mnemonic(_("I_nstant updates"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(*pinstant), *target);
    gtk_box_pack_start(GTK_BOX(hbox), *pinstant, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(*pinstant), "target", target);
    g_signal_connect_swapped(*pinstant, "toggled",
                             G_CALLBACK(gwy_synth_boolean_changed), controls);
    g_signal_connect(*pinstant, "toggled",
                     G_CALLBACK(gwy_synth_toggle_sensitive), *pupdate);

    return hbox;
}

G_GNUC_UNUSED
static GtkWidget*
gwy_synth_progressive_preview_new(GWY_SYNTH_CONTROLS *controls,
                                  GtkWidget **pupdate,
                                  GtkWidget **panimated,
                                  gboolean *target)
{
    GtkWidget *hbox;

    hbox = gtk_hbox_new(FALSE, 6);

    *pupdate = gtk_button_new_with_mnemonic(_("_Update"));
    gtk_box_pack_start(GTK_BOX(hbox), *pupdate, FALSE, FALSE, 0);

    *panimated = gtk_check_button_new_with_mnemonic(_("Progressive preview"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(*panimated), *target);
    gtk_box_pack_start(GTK_BOX(hbox), *panimated, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(*panimated), "target", target);
    g_signal_connect_swapped(*panimated, "toggled",
                             G_CALLBACK(gwy_synth_boolean_changed), controls);

    return hbox;
}

G_GNUC_UNUSED
static void
gwy_synth_randomize_seed(GtkAdjustment *adj)
{
    /* Use the GLib's global PRNG for seeding */
    gtk_adjustment_set_value(adj, g_random_int() & 0x7fffffff);
}

G_GNUC_UNUSED
static GtkWidget*
gwy_synth_random_seed_new(GWY_SYNTH_CONTROLS *controls,
                          GtkObject **adj,
                          gint *target)
{
    GtkWidget *hbox, *button, *label, *spin;

    hbox = gtk_hbox_new(FALSE, 6);

    *adj = gtk_adjustment_new(*target, 1, 0x7fffffff, 1, 10, 0);
    g_object_set_data(G_OBJECT(*adj), "target", target);
    g_signal_connect_swapped(*adj, "value-changed",
                             G_CALLBACK(gwy_synth_int_changed), controls);

    label = gtk_label_new_with_mnemonic(_("R_andom seed:"));
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    spin = gtk_spin_button_new(GTK_ADJUSTMENT(*adj), 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), spin);
    gtk_box_pack_start(GTK_BOX(hbox), spin, FALSE, FALSE, 0);

    button = gtk_button_new_with_mnemonic(gwy_sgettext("seed|_New"));
    gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(gwy_synth_randomize_seed), *adj);

    return hbox;
}

G_GNUC_UNUSED
static GtkWidget*
gwy_synth_randomize_new(gboolean *target)
{
    GtkWidget *button = gtk_check_button_new_with_mnemonic(_("Randomi_ze"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), *target);
    g_signal_connect(button, "toggled",
                     G_CALLBACK(gwy_synth_boolean_changed_silent), target);
    return button;
}

#endif

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
