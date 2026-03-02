/*
 *  $Id: wait.h 24545 2021-11-25 14:51:19Z yeti-dn $
 *  Copyright (C) 2004-2018 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_APP_WAIT_H__
#define __GWY_APP_WAIT_H__

#include <gtk/gtkwindow.h>

G_BEGIN_DECLS

void       gwy_app_wait_start             (GtkWindow *window,
                                           const gchar *message);
void       gwy_app_wait_finish            (void);
gboolean   gwy_app_wait_set_fraction      (gdouble fraction)      G_GNUC_WARN_UNUSED_RESULT;
gboolean   gwy_app_wait_set_message       (const gchar *message)  G_GNUC_WARN_UNUSED_RESULT;
gboolean   gwy_app_wait_set_message_prefix(const gchar *prefix)   G_GNUC_WARN_UNUSED_RESULT;
void       gwy_app_wait_cursor_start      (GtkWindow *window);
void       gwy_app_wait_cursor_finish     (GtkWindow *window);
gboolean   gwy_app_wait_get_enabled       (void);
void       gwy_app_wait_set_enabled       (gboolean setting);
gboolean   gwy_app_wait_was_canceled      (void);
void       gwy_app_wait_set_preview_widget(GtkWidget *widget);

G_END_DECLS

#endif /* __GWY_APP_WAIT_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
