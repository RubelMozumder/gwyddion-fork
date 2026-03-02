/*
 *  $Id: graph_export_vector.c 24412 2021-10-22 15:47:51Z yeti-dn $
 *  Copyright (C) 2006-2021 David Necas (Yeti), Petr Klapetek.
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-graph.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

static gboolean module_register(void);
static void     export         (GwyGraph *graph);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Exports graphs to PostScript"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2006",
};

GWY_MODULE_QUERY2(module_info, graph_export_vector)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_export_vector",
                            (GwyGraphFunc)&export,
                            N_("/_Export/_PostScript"),
                            GWY_STOCK_GRAPH_EXPORT_VECTOR,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Export graph to PostScript"));

    return TRUE;
}

static void
export(GwyGraph *graph)
{
    GString *str = gwy_graph_export_postscript(graph, TRUE, TRUE, TRUE, NULL);
    gwy_save_auxiliary_data(_("Export to PostScript"), NULL, str->len, str->str);
    g_string_free(str, TRUE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
