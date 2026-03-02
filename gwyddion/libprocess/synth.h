/*
 *  $Id: synth.h 23460 2021-04-11 17:02:45Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti).
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

#ifndef __GWY_PROCESS_SYNTH_H__
#define __GWY_PROCESS_SYNTH_H__

#include <libgwyddion/gwymath.h>
#include <libprocess/datafield.h>

G_BEGIN_DECLS

void gwy_data_field_synth_gaussian_displacement(GwyDataField *data_field,
                                                gdouble sigma,
                                                gdouble tau,
                                                GRand *rng);

G_END_DECLS

#endif

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
