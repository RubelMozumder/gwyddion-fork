/*
 *  $Id: gwysivalueformat.h 20677 2017-12-18 18:22:52Z yeti-dn $
 *  Copyright (C) 2016 David Necas (Yeti).
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

#ifndef __GWY_SI_VALUE_FORMAT_H__
#define __GWY_SI_VALUE_FORMAT_H__

#include <glib-object.h>
#include <libgwyddion/gwyddionenums.h>

G_BEGIN_DECLS

#define GWY_TYPE_SI_VALUE_FORMAT          (gwy_si_value_format_get_type())

typedef struct {
    /*<public>*/
    gdouble magnitude;
    gint precision;
    gchar *units;
    /*<private>*/
    GString *units_gstring;
} GwySIValueFormat;

GType             gwy_si_value_format_get_type      (void)                      G_GNUC_CONST;
GwySIValueFormat* gwy_si_unit_value_format_new      (gdouble magnitude,
                                                     gint precision,
                                                     const gchar *units)        G_GNUC_MALLOC;
GwySIValueFormat* gwy_si_unit_value_format_copy     (GwySIValueFormat *format)  G_GNUC_MALLOC;
void              gwy_si_unit_value_format_free     (GwySIValueFormat *format);
GwySIValueFormat* gwy_si_unit_value_format_clone    (GwySIValueFormat *source,
                                                     GwySIValueFormat *dest);
void              gwy_si_unit_value_format_set_units(GwySIValueFormat *format,
                                                     const gchar *units);

G_END_DECLS

#endif /* __GWY_SI_VALUE_FORMAT_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
