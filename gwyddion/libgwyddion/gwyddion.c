/*
 *  $Id: gwyddion.c 22336 2019-07-25 09:26:36Z yeti-dn $
 *  Copyright (C) 2003-2019 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwyddion.h>
#include "gwyddioninternal.h"
#include "gwyomp.h"

/**
 * gwy_type_init:
 *
 * Makes libgwyddion types safe for deserialization and performs other
 * initialization.  You have to call this function from the main thread before
 * using objects from libgwyddion.
 *
 * It calls g_type_init() first to make sure GLib object system is initialized.
 *
 * It is safe to call this function more than once, subsequent calls are no-op.
 **/
void
gwy_type_init(void)
{
    static gboolean types_initialized = FALSE;

    if (types_initialized)
        return;

#if (!GLIB_CHECK_VERSION(2, 36, 0))
    g_type_init();
#endif

    _gwy_init_fftw_threads();

    /* Disable nesting since it is essentially always bad if we do not do
     * something like a slowly branching recursion (which we don't) and even
     * then some sane limits would have to be set. GOMP sets both
     * max-active-levels and thread-limit to something like 2^{31}-1, which is
     * hardly sane. Library users can override this later, but at their own
     * risk.
     *
     * It seems omp_get_max_threads() still returns counts >= 1 inside an
     * active parallel region.  This means we consume extra resources when
     * preallocating memory for threads, for instance.  To really prevent
     * nesting we would have to do it manually using omp_get_active_levels()...
     */
#ifdef _OPENMP
    omp_set_max_active_levels(1);
#endif

    g_type_class_peek(GWY_TYPE_SI_UNIT);
    g_type_class_peek(GWY_TYPE_CONTAINER);
    g_type_class_peek(GWY_TYPE_INVENTORY);
    g_type_class_peek(GWY_TYPE_RESOURCE);
    g_type_class_peek(GWY_TYPE_NLFIT_PRESET);
    g_type_class_peek(GWY_TYPE_FD_CURVE_PRESET);
    g_type_class_peek(GWY_TYPE_STRING_LIST);
    g_type_class_peek(GWY_TYPE_XY);
    g_type_class_peek(GWY_TYPE_XYZ);
    g_type_class_peek(GWY_TYPE_ENUM);
    g_type_class_peek(GWY_TYPE_SI_VALUE_FORMAT);
    types_initialized = TRUE;

    _gwy_nlfit_preset_class_setup_presets();
    _gwy_fd_curve_preset_class_setup_presets();
}

/************************** Documentation ****************************/

/**
 * SECTION:gwyddion
 * @title: gwyddion
 * @short_description: Base functions, library initialization
 * @see_also: #GwySerializable
 *
 * Gwyddion classes has to be initialized before they can be safely
 * deserialized. The function gwy_type_init() performs this initialization.
 **/

/**
 * SECTION:gwyddionenums
 * @title: gwyddionenums
 * @short_description: Common libgwyddion enumerations
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
