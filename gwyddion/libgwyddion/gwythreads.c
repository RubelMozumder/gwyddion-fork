/*
 *  $Id: gwythreads.c 25924 2023-10-24 09:28:07Z yeti-dn $
 *  Copyright (C) 2019 David Necas (Yeti), Petr Klapetek.
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
#include <fftw3.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include <libgwyddion/gwythreads.h>
#include "gwyddioninternal.h"

static gboolean threads_enabled = FALSE;

/**
 * gwy_threads_are_enabled:
 *
 * Obtains the state of internal multithread processing.
 *
 * Returns: %TRUE if multithread processing is enabled; %FALSE otherwise (this includes the case of Gwyddion not built
 *          with multithread processing support at all).
 *
 * Since: 2.53
 **/
gboolean
gwy_threads_are_enabled(void)
{
    return threads_enabled;
}

/**
 * gwy_threads_set_enabled:
 * @setting: %TRUE to enable multithread processing; %FALSE to disable it.
 *
 * Enables or disables internal multithread processing.
 *
 * This function can be called any time during the program life time to switch between single- and multithread
 * processing.  It must be called from the main thread while no Gwyddion data processing functions are being executed.
 *
 * Since: 2.53
 **/
void
gwy_threads_set_enabled(G_GNUC_UNUSED gboolean setting)
{
#ifdef _OPENMP
    if (setting != threads_enabled && omp_in_parallel()) {
#pragma omp barrier
        g_critical("Gwyddion mutithreading is being enabled/disabled inside parallel OpenMP code.");
    }
    threads_enabled = setting;
#endif
}

void
_gwy_init_fftw_threads(void)
{
    /* Function fftw_init_threads() must be called before any other FFTW function.  We cannot really ensure someone
     * else has not called some FFTW function before.  This at least ensures we do not. */
#ifdef HAVE_FFTW_WITH_OPENMP
    if (!fftw_init_threads())
        g_warning("Cannot initialise FFTW threads.");
#endif
}

/************************** Documentation ****************************/

/**
 * SECTION:gwythreads
 * @title: gwythreads
 * @short_description: Multithread processing control
 *
 * Gwyddion can utilise multithread processing via OpenMP.
 *
 * It is disabled by default.  If it is enabled, is utilised internally and transparently in Gwyddion functions.  No
 * threads are exposed in the API.
 *
 * The only exception is that when multithread processing is enabled, user-supplied routines called during data
 * processing such as #GwyNLFitFunc or #GwyCooordTransform2DFunc may be called from multiple threads and must be
 * reentrant.  This does not apply to #GwySetMessageFunc and #GwySetFractionFunc which are always called from the main
 * (GUI) thread.
 *
 * If you run programs or scripts based on Gwyddion in parallel, for instance in a simulation or batch data
 * processing, it is recommended to keep multithread processing disabled.  For GUI programs (like Gwyddion itself) or
 * tasks run serially, it can be useful to enable it.
 *
 * If Gwyddion was not built with multithread processing support, enabling threads does not do anything and
 * gwy_threads_are_enabled() will continue to return %FALSE.
 *
 * If Gwyddion is built with OpenMP-enabled FFTW, it calls fftw_init_threads() when threads are enabled and can employ
 * multithreaded FFTW.  When mixing Gwyddion functions with direct FFTW utilisation, call fftw_plan_with_nthreads()
 * with your preferred number of threads before you create a plan.
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
