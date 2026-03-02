/*
 *  $Id: gwycheckboxes.h 21106 2018-05-27 10:20:06Z yeti-dn $
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

#ifndef __GWY_CHECK_BOXES_H__
#define __GWY_CHECK_BOXES_H__

#include <gtk/gtktable.h>
#include <libgwyddion/gwyenum.h>

G_BEGIN_DECLS

GSList*    gwy_check_boxes_create         (const GwyEnum *entries,
                                           gint nentries,
                                           GCallback callback,
                                           gpointer cbdata,
                                           guint selected);
GSList*    gwy_check_boxes_createl        (GCallback callback,
                                           gpointer cbdata,
                                           guint selected,
                                           ...);
gint       gwy_check_boxes_attach_to_table(GSList *group,
                                           GtkTable *table,
                                           gint colspan,
                                           gint row);
void       gwy_check_boxes_set_selected   (GSList *group,
                                           guint selected);
guint      gwy_check_boxes_get_selected   (GSList *group);
GSList*    gwy_check_box_get_group        (GtkWidget *checkbox);
GtkWidget* gwy_check_boxes_find           (GSList *group,
                                           guint value);
void       gwy_check_boxes_set_sensitive  (GSList *group,
                                           gboolean sensitive);
guint      gwy_check_box_get_value        (GtkWidget *checkbox);

G_END_DECLS

#endif /* __GWY_CHECK_BOXES_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

