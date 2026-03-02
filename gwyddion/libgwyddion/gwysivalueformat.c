/*
 *  $Id: gwysivalueformat.c 25969 2023-10-27 14:47:05Z yeti-dn $
 *  Copyright (C) 2016 David Necas (Yeti).
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

#include "config.h"
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwysivalueformat.h>

GType
gwy_si_value_format_get_type(void)
{
    /* Threads: type registered from gwy_types_init(). */
    static GType type = 0;

    if (G_UNLIKELY(!type)) {
        type = g_boxed_type_register_static
                        ("GwySIValueFormat",
                         (GBoxedCopyFunc)&gwy_si_unit_value_format_copy,
                         (GBoxedFreeFunc)&gwy_si_unit_value_format_free);
    }

    return type;
}

/**
 * gwy_si_unit_value_format_new:
 * @magnitude: Number to divide a quantity by (a power of 1000).
 * @precision: Number of decimal places to format a quantity to.
 * @units: Units to put after quantity divided by @magnitude.
 *
 * Constructs a new value format directly.
 *
 * Usually you construct value formats from a #GwySIUnit using functions such as gwy_si_unit_get_format_with_digits()
 * or obtain it from data object functions.
 *
 * Returns: Newly allocated value format.
 *
 * Since: 2.46
 **/
GwySIValueFormat*
gwy_si_unit_value_format_new(gdouble magnitude,
                             gint precision,
                             const gchar *units)
{
    GwySIValueFormat *vf = g_new(GwySIValueFormat, 1);
    vf->magnitude = magnitude;
    vf->precision = precision;
    vf->units_gstring = g_string_new(units);
    vf->units = vf->units_gstring->str;
    return vf;
}

/**
 * gwy_si_unit_value_format_copy:
 * @format: A value format to copy.
 *
 * Copies a value format structure.
 *
 * Returns: Newly allocated value format, identical to @format.
 *
 * Since: 2.39
 **/
GwySIValueFormat*
gwy_si_unit_value_format_copy(GwySIValueFormat *format)
{
    GwySIValueFormat *vf;

    g_return_val_if_fail(format, NULL);
    vf = g_memdup(format, sizeof(GwySIValueFormat));
    vf->units_gstring = g_string_new(format->units);
    vf->units = vf->units_gstring->str;
    return vf;
}

/**
 * gwy_si_unit_value_format_free:
 * @format: A value format to free.
 *
 * Frees a value format structure.
 **/
void
gwy_si_unit_value_format_free(GwySIValueFormat *format)
{
    g_return_if_fail(format);
    if (format->units_gstring)
        g_string_free(format->units_gstring, TRUE);
    g_free(format);
}

/**
 * gwy_si_unit_value_format_clone:
 * @source: Source value format.
 * @dest: Destination value format, or %NULL.
 *
 * Clones a value format to another.
 *
 * This function follows the convention of many value format updating functions that can either modify an existing
 * format or allocate a new one.
 *
 * Returns: The @dest value format.  If it was %NULL, a newly allocated format is returned, otherwise (modified) @dest
 *          itself is returned.
 *
 * Since: 2.46
 **/
GwySIValueFormat*
gwy_si_unit_value_format_clone(GwySIValueFormat *source,
                               GwySIValueFormat *dest)
{
    g_return_val_if_fail(source, NULL);
    if (!dest) {
        return gwy_si_unit_value_format_new(source->magnitude,
                                            source->precision,
                                            source->units);
    }
    dest->magnitude = source->magnitude;
    dest->precision = source->precision;
    gwy_si_unit_value_format_set_units(dest, source->units);

    return dest;
}

/**
 * gwy_si_unit_value_format_set_units:
 * @format: A value format to set units of.
 * @units: The units string.
 *
 * Sets the units field of a value format structure.
 *
 * This function keeps the @units and @units_gstring fields consistent.
 **/
void
gwy_si_unit_value_format_set_units(GwySIValueFormat *format,
                                   const gchar *units)
{
    g_return_if_fail(format);
    if (!format->units_gstring)
        format->units_gstring = g_string_new(units);
    else
        g_string_assign(format->units_gstring, units);

    format->units = format->units_gstring->str;
}

/************************** Documentation ****************************/

/**
 * SECTION:gwysivalueformat
 * @title: GwySIValueFormat
 * @short_description: Physical quantitiy formatting
 *
 * #GwySIValueFormat object represents instructions how to format numbers representing physical quantities, including
 * precision and units.  They are usually created based on a #GwySIUnit and some data ranges with functions such as
 * gwy_si_unit_get_format_with_resolution() or gwy_si_unit_get_format_with_digits().  Various data objects also offer
 * method for obtaining value formats representing reasonably their coordinates or values, for instance
 * gwy_data_field_get_value_format_xy().
 **/

/**
 * GwySIValueFormat:
 * @magnitude: Number to divide a quantity by (a power of 1000).
 * @precision: Number of decimal places to format a quantity to.
 * @units: Units to put after quantity divided by @magnitude.  This field must be considered read-only as it is
 *         actually an alias to @units_gstring->str.
 *
 * A physical quantity formatting information.
 *
 * The @magnitude and @precision fields can be directly modified if necessary. Units must be always set with
 * gwy_si_unit_value_format_set_units() to update the internal representation correctly.
 */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
