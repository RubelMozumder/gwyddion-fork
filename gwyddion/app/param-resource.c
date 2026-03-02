/*
 *  $Id: param-resource.c 24986 2022-09-02 09:48:21Z yeti-dn $
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

#include "config.h"
#include "libgwyddion/gwymacros.h"
#include "param-resource.h"
#include "param-internal.h"

#define DEFAULT_PARAMS_NAME ""

typedef struct {
    GType type;
    GwyParamDef *pardef;
    const gchar *resource_dirname;
} ParamResourceInfo;

/* Base class for resource classes creates from presets. */
typedef struct _GwyParamResource      GwyParamResource;
typedef struct _GwyParamResourceClass GwyParamResourceClass;

struct _GwyParamResource {
    GwyResource parent_instance;
    GwyParams *params;
};

struct _GwyParamResourceClass {
    GwyResourceClass parent_class;
    GwyParamDef *pardef;
    GHashTable *parmap;
};

/* Template for all the derived classes; they are all the same. */
typedef struct _GwyParamResourceDefined      GwyParamResourceDefined;
typedef struct _GwyParamResourceDefinedClass GwyParamResourceDefinedClass;

struct _GwyParamResourceDefined {
    GwyParamResource parent_instance;
};

struct _GwyParamResourceDefinedClass {
    GwyParamResourceClass parent_class;
};

static void         gwy_param_resource_finalize          (GObject *object);
static gpointer     gwy_param_resource_copy              (gpointer item);
static void         gwy_param_resource_dump              (GwyResource *resource,
                                                          GString *str);
static GwyResource* gwy_param_resource_parse             (GType type,
                                                          const gchar *text,
                                                          gboolean is_const);
static void         gwy_param_resource_defined_class_init(GwyParamResourceDefinedClass *klass);
static void         gwy_param_resource_defined_init      (GwyParamResourceDefined *resource);

static GArray *resource_info = NULL;

G_DEFINE_ABSTRACT_TYPE(GwyParamResource, gwy_param_resource, GWY_TYPE_RESOURCE)

static void
gwy_param_resource_class_init(GwyParamResourceClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GwyResourceClass *res_class = GWY_RESOURCE_CLASS(klass);

    gobject_class->finalize = gwy_param_resource_finalize;

    res_class->dump = gwy_param_resource_dump;
    res_class->parse_with_type = gwy_param_resource_parse;
}

static void
gwy_param_resource_init(GwyParamResource *resource)
{
    /* Can create params, but cannot set def because we are not of any defined type yet here. */
    resource->params = gwy_params_new();
}

static void
gwy_param_resource_finalize(GObject *object)
{
    GwyParamResource *resource = (GwyParamResource*)object;

    GWY_OBJECT_UNREF(resource->params);

    G_OBJECT_CLASS(gwy_param_resource_parent_class)->finalize(object);
}

static gpointer
gwy_param_resource_copy(gpointer item)
{
    GType type = G_TYPE_FROM_INSTANCE(item);
    GwyParamResource *presource = GWY_PARAM_RESOURCE(item);
    GwyParamResource *copy = g_object_new(type, "is-const", FALSE, NULL);

    /* FIXME: This is brutal, but we need gwy_params_assign() for a cleaner way. */
    g_object_unref(copy->params);
    copy->params = gwy_params_duplicate(presource->params);

    g_string_assign(GWY_RESOURCE(copy)->name, gwy_resource_get_name(GWY_RESOURCE(presource)));
    /* New copies start as modified. */
    GWY_RESOURCE(copy)->is_modified = TRUE;

    return copy;
}

static void
gwy_param_resource_dump(GwyResource *resource, GString *str)
{
    GwyParamResource *presource = (GwyParamResource*)resource;

    _gwy_params_dump_to_string(presource->params, str);
}

static GwyResource*
gwy_param_resource_parse(GType type, const gchar *text, gboolean is_const)
{
    GwyResource *resource = g_object_new(type, "is-const", is_const, NULL);
    GwyParamResource *presource = (GwyParamResource*)resource;
    GwyParamResourceClass *paramres_class = GWY_PARAM_RESOURCE_CLASS(G_OBJECT_GET_CLASS(resource));

    _gwy_params_parse_string(presource->params, text, paramres_class->parmap);

    return resource;
}

/**
 * gwy_param_resource_get_params:
 * @resource: A resource holding a pararameter set.
 *
 * Gets the set of parameter values of a parameter resource.
 *
 * Returns: The parameter set.
 *
 * Since: 2.62
 **/
GwyParams*
gwy_param_resource_get_params(GwyParamResource *resource)
{
    g_return_val_if_fail(GWY_IS_PARAM_RESOURCE(resource), NULL);
    return resource->params;
}

/**
 * gwy_param_def_make_resource_type:
 * @pardef: A set of parameter definitions.
 * @glibtypename: Type name in the GLib type system to create, for instance GwyRawFilePreset.
 * @resname: Resource name, used as directory name (@name field in #GwyResourceClass).
 *
 * Creates a resource class for holding parameters of given type.
 *
 * The created resource can be used to handle module presets without having to implement loading and saving the
 * parameters from and to files.
 *
 * If @pardef has already set function name with gwy_param_def_set_function_name() (recommended), @resname can be
 * %NULL to use the @pardef's function name.
 *
 * Returns: A newly registered type id in the GLib type system.
 *
 * Since: 2.62
 **/
GType
gwy_param_def_make_resource_type(GwyParamDef *pardef,
                                 const gchar *glibtypename,
                                 const gchar *resname)
{
    GwyParamResourceClass *paramres_class;
    GwyParamResource *default_item;
    GwyInventory *inventory;
    ParamResourceInfo info;
    GType type;

    g_return_val_if_fail(GWY_IS_PARAM_DEF(pardef), 0);
    g_return_val_if_fail(glibtypename, 0);
    if (!resname)
        resname = gwy_param_def_get_function_name(pardef);
    g_return_val_if_fail(resname, 0);

    if ((type = g_type_from_name(glibtypename))) {
        g_warning("Attempting to register resource class %s which already exists.", glibtypename);
        return type;
    }

    if (!resource_info)
        resource_info = g_array_new(FALSE, FALSE, sizeof(ParamResourceInfo));

    type = g_type_register_static_simple(GWY_TYPE_PARAM_RESOURCE, g_intern_string(glibtypename),
                                         sizeof(GwyParamResourceDefinedClass),
                                         (GClassInitFunc)gwy_param_resource_defined_class_init,
                                         sizeof(GwyParamResourceDefined),
                                         (GInstanceInitFunc)gwy_param_resource_defined_init,
                                         0);

    info.type = type;
    info.resource_dirname = g_intern_string(resname);
    info.pardef = g_object_ref(pardef);
    g_array_append_val(resource_info, info);

    /* Force class to exist and create the default parameters item. */
    default_item = g_object_new(type, "is-const", TRUE, NULL);
    GWY_RESOURCE(default_item)->is_modified = FALSE;
    g_string_assign(GWY_RESOURCE(default_item)->name, DEFAULT_PARAMS_NAME);

    paramres_class = GWY_PARAM_RESOURCE_GET_CLASS(default_item);
    inventory = gwy_resource_class_get_inventory(GWY_RESOURCE_CLASS(paramres_class));
    gwy_inventory_insert_item(inventory, default_item);
    g_object_unref(default_item);

    return type;
}

static void
gwy_param_resource_defined_init(GwyParamResourceDefined *resource)
{
    GwyParamResourceClass *paramres_class = GWY_PARAM_RESOURCE_GET_CLASS(resource);

    gwy_params_set_def(GWY_PARAM_RESOURCE(resource)->params, paramres_class->pardef);
}

static void
gwy_param_resource_defined_class_init(GwyParamResourceDefinedClass *klass)
{
    GType type = G_TYPE_FROM_CLASS(klass);
    GwyParamResourceClass *par_class = GWY_PARAM_RESOURCE_CLASS(klass);
    GwyResourceClass *res_class = GWY_RESOURCE_CLASS(klass);
    GwyParamDef *pardef;
    GHashTable *parmap;
    ParamResourceInfo *info;
    guint i, n;

    if (!resource_info)
        resource_info = g_array_new(FALSE, FALSE, sizeof(ParamResourceInfo));

    n = resource_info->len;
    for (i = 0; i < n; i++) {
        info = &g_array_index(resource_info, ParamResourceInfo, i);
        if (info->type == type)
            break;
    }
    g_return_if_fail(i < n);

    res_class->item_type = *gwy_resource_class_get_item_type(res_class);
    res_class->item_type.type = G_TYPE_FROM_CLASS(klass);
    res_class->item_type.copy = gwy_param_resource_copy;
    res_class->name = info->resource_dirname;
    res_class->inventory = gwy_inventory_new(&res_class->item_type);
    gwy_inventory_set_default_item_name(res_class->inventory, DEFAULT_PARAMS_NAME);

    pardef = par_class->pardef = info->pardef;
    parmap = par_class->parmap = g_hash_table_new(g_str_hash, g_str_equal);
    n = _gwy_param_def_size(pardef);
    for (i = 0; i < n; i++) {
        const GwyParamDefItem *def = _gwy_param_def_item(pardef, i);
        g_hash_table_insert(parmap, (gpointer)def->name, GUINT_TO_POINTER(i+1));
    }
}

/**
 * SECTION:param-resource
 * @title: GwyParamResource
 * @short_description: Resources holding set of parameters
 *
 * #GwyParamResource represents a resource holding a #GwyParams set of module parameter values. It can be useful to
 * implement parameter presets, stored on disk.
 *
 * It is an abstract base class. Modules have to create a specific class for their presets using
 * gwy_param_def_make_resource_type() which constructs, registers and returns a new #Gtype for the specific resource
 * class.
 **/

/**
 * GwyParamResource:
 *
 * Abstract object representing a resource holding a set of parameter values.
 *
 * The #GwyParamResource struct contains no public fields.
 *
 * Since: 2.62
 **/

/**
 * GwyParamResourceClass:
 *
 * Abstract class of resources holding parameter value sets.
 *
 * Since: 2.62
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
