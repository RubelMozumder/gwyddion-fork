/*
 *  $Id: gwyprocess.c 23915 2021-08-03 08:31:13Z yeti-dn $
 *  Copyright (C) 2003-2004 David Necas (Yeti), Petr Klapetek.
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

#include <fftw3.h>
#include <libgwyddion/gwyddion.h>
#include <libprocess/gwyprocess.h>
#include "gwyprocessinternal.h"

static void
gwy_process_import_fftw_wisdom(void)
{
    G_GNUC_UNUSED gboolean ok;

    ok = fftw_import_system_wisdom();
    gwy_debug("FFTW3 system wisdom imported: %d", ok);
}

/**
 * gwy_process_type_init:
 *
 * Makes libgwyprocess types safe for deserialization and performs other
 * initialization.  You have to call this function from the main thread before
 * using objects from libgwyprocess.
 *
 * It calls gwy_type_init() first to make sure libgwyddion is initialized.
 *
 * It is safe to call this function more than once, subsequent calls are no-op.
 **/
void
gwy_process_type_init(void)
{
    static gboolean types_initialized = FALSE;

    if (types_initialized)
        return;

    gwy_type_init();

    g_type_class_peek(GWY_TYPE_DATA_LINE);
    g_type_class_peek(GWY_TYPE_DATA_FIELD);
    g_type_class_peek(GWY_TYPE_BRICK);
    g_type_class_peek(GWY_TYPE_CDLINE);
    g_type_class_peek(GWY_TYPE_SPECTRA);
    g_type_class_peek(GWY_TYPE_SURFACE);
    g_type_class_peek(GWY_TYPE_LAWN);
    g_type_class_peek(GWY_TYPE_CALDATA);
    g_type_class_peek(GWY_TYPE_TRIANGULATION);
    g_type_class_peek(GWY_TYPE_PEAKS);
    g_type_class_peek(GWY_TYPE_SPLINE);
    g_type_class_peek(GWY_TYPE_TIP_MODEL_PRESET);
    types_initialized = TRUE;

    _gwy_cdline_class_setup_presets();
    _gwy_grain_value_class_setup_presets();
    _gwy_shape_fit_preset_class_setup_presets();
    _gwy_calibration_class_setup_presets();
    gwy_process_import_fftw_wisdom();
}

/* Transfer units by value (gwy_foo_copy_units()). */
void
_gwy_copy_si_unit(GwySIUnit *source, GwySIUnit **dest)
{
    if (source == *dest)
        return;

    if (!source || gwy_si_unit_equal_string(source, NULL)) {
        GWY_OBJECT_UNREF(*dest);
        return;
    }

    if (*dest)
        gwy_si_unit_assign(*dest, source);
    else
        *dest = gwy_si_unit_duplicate(source);
}

/* Take unit object ownership -- @objmember is presumably an object member
 * (gwy_foo_set_si_unit_x()). */
void
_gwy_set_object_si_unit(GwySIUnit *unit, GwySIUnit **objmember)
{
    GwySIUnit *tmp;

    g_return_if_fail(!unit || GWY_IS_SI_UNIT(unit));

    if (unit == *objmember)
        return;

    if (!unit || gwy_si_unit_equal_string(unit, NULL)) {
        GWY_OBJECT_UNREF(*objmember);
        return;
    }

    tmp = *objmember;
    *objmember = g_object_ref(unit);
    GWY_OBJECT_UNREF(tmp);
}

/************************** Documentation ****************************/

/**
 * SECTION:gwyprocess
 * @title: gwyprocess
 * @short_description: Base functions
 *
 * Gwyddion classes has to be initialized before they can be safely
 * deserialized. The function gwy_process_type_init() performs this
 * initialization.
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
