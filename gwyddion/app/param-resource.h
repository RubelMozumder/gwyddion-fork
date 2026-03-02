/*
 *  $Id: param-resource.h 24980 2022-09-01 12:24:23Z yeti-dn $
 *  Copyright (C) 2022 David Necas (Yeti).
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

#ifndef __GWY_PARAM_RESOURCE_H__
#define __GWY_PARAM_RESOURCE_H__

#include <glib-object.h>
#include <app/params.h>

G_BEGIN_DECLS

#define GWY_TYPE_PARAM_RESOURCE            (gwy_param_resource_get_type())
#define GWY_PARAM_RESOURCE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_PARAM_RESOURCE, GwyParamResource))
#define GWY_PARAM_RESOURCE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_PARAM_RESOURCE, GwyParamResourceClass))
#define GWY_IS_PARAM_RESOURCE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_PARAM_RESOURCE))
#define GWY_IS_PARAM_RESOURCE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_PARAM_RESOURCE))
#define GWY_PARAM_RESOURCE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_PARAM_RESOURCE, GwyParamResourceClass))

typedef struct _GwyParamResource      GwyParamResource;
typedef struct _GwyParamResourceClass GwyParamResourceClass;

GType gwy_param_resource_get_type(void) G_GNUC_CONST;

GType gwy_param_def_make_resource_type(GwyParamDef *pardef,
                                       const gchar *glibtypename,
                                       const gchar *resname);

GwyParams* gwy_param_resource_get_params(GwyParamResource *resource);

G_END_DECLS

#endif

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
