/*
 *  $Id: param-def.c 25756 2023-10-02 14:07:38Z yeti-dn $
 *  Copyright (C) 2021-2023 David Necas (Yeti).
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
#include <stdlib.h>
#include "libgwyddion/gwymacros.h"
#include "libprocess/gwyprocesstypes.h"
#include "libprocess/gwyprocessenums.h"
#include "gwyapptypes.h"
#include "param-def.h"
#include "param-internal.h"

#define DEBUG_USERS

typedef struct _GwyParamDefPrivate GwyParamDefPrivate;

struct _GwyParamDefPrivate {
    const gchar *function_name;
    GwyParamDefItem *defs;
    gint *ids;
    guint ndefs;
    guint nalloc;
    gint maxid;       /* Keeping maximum used id makes typical param construction linear-time. */
    gboolean is_used;
#ifdef DEBUG_USERS
    GSList *users;
#endif
};

static void         gwy_param_def_finalize       (GObject *gobject);
static void         gwy_param_def_append         (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  GwyParamDefItem *item);
static void         gwy_param_def_append_enum    (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  GType gtype,
                                                  const GwyEnum *values,
                                                  gint nvalues,
                                                  gint default_value);
static void         gwy_param_def_append_flags   (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  GType gtype,
                                                  const GwyEnum *values,
                                                  gint nvalues,
                                                  guint default_value);
static gchar*       rectify_string               (const gchar *value,
                                                  GwyParamStringFlags flags,
                                                  GwyRectifyStringFunc rectify);
static gint         find_param_def               (GwyParamDefPrivate *priv,
                                                  gint id);

G_DEFINE_TYPE(GwyParamDef, gwy_param_def, G_TYPE_OBJECT);

static void
gwy_param_def_class_init(GwyParamDefClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_param_def_finalize;

    g_type_class_add_private(klass, sizeof(GwyParamDefPrivate));
}

static void
gwy_param_def_init(GwyParamDef *pardef)
{
    GwyParamDefPrivate *priv;

    pardef->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE(pardef, GWY_TYPE_PARAM_DEF, GwyParamDefPrivate);
    priv->maxid = -1;
}

static void
gwy_param_def_finalize(GObject *gobject)
{
    GwyParamDef *pardef = (GwyParamDef*)gobject;
    GwyParamDefPrivate *priv = pardef->priv;
    guint i;

    for (i = 0; i < priv->ndefs; i++) {
        if (priv->defs[i].type == GWY_PARAM_STRING)
            g_free(priv->defs[i].def.s.default_value);
    }
    g_free(priv->ids);
    g_free(priv->defs);
    G_OBJECT_CLASS(gwy_param_def_parent_class)->finalize(gobject);
}

/**
 * gwy_param_def_new:
 *
 * Creates a new empty set of parameter definitions.
 *
 * Definitions can be added only during construction, i.e. until the first time it is used to create #GwyParams.  Then
 * the definition set must be considered immutable.
 *
 * Returns: A newly create paramete definition set.
 *
 * Since: 2.59
 **/
GwyParamDef*
gwy_param_def_new(void)
{
    return g_object_new(GWY_TYPE_PARAM_DEF, NULL);
}

/**
 * gwy_param_def_set_function_name:
 * @pardef: A set of parameter definitions.
 * @name: Settings function name, or %NULL.
 *
 * Sets the settings function name of a set of parameter definitions.
 *
 * Usually the function name should be given simply as gwy_process_func_current() (or analogous functions for other
 * module types).  The exception is functions with settings keys different from module function name.  If the name is
 * unset then #GwyParams settings functions like gwy_params_new_from_settings() cannot be used.
 *
 * The function name can be set freely even after construction.  This allows having just a single set of parameter
 * definitions for multiple related module functions which all have the same parameters.
 *
 * Since: 2.59
 **/
void
gwy_param_def_set_function_name(GwyParamDef *pardef,
                                const gchar *name)
{
    g_return_if_fail(GWY_IS_PARAM_DEF(pardef));
    pardef->priv->function_name = name;
}

/**
 * gwy_param_def_get_function_name:
 * @pardef: A set of parameter definitions.
 *
 * Gets the settings function name of a set of parameter definitions.
 *
 * Returns: The functions name (possibly %NULL).
 *
 * Since: 2.59
 **/
const gchar*
gwy_param_def_get_function_name(GwyParamDef *pardef)
{
    g_return_val_if_fail(GWY_IS_PARAM_DEF(pardef), NULL);
    return pardef->priv->function_name;
}

/**
 * gwy_param_def_get_param_name:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 *
 * Gets the name of an already defined parameter in a set of parameter definitions.
 *
 * See gwy_param_def_get_param_label() for parameter labels.
 *
 * Returns: The parameter name, as a string owned by @pardef. It may be %NULL if the parameter is not stored to
 * settings.
 *
 * Since: 2.62
 **/
const gchar*
gwy_param_def_get_param_name(GwyParamDef *pardef,
                             gint id)
{
    const GwyParamDefItem *item;

    g_return_val_if_fail(GWY_IS_PARAM_DEF(pardef), NULL);
    item = _gwy_param_def_item(pardef, _gwy_param_def_index(pardef, id));
    g_return_val_if_fail(item, NULL);
    return item->name;
}

/**
 * gwy_param_def_get_param_label:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 *
 * Gets the label of an already defined parameter in a set of parameter definitions.
 *
 * See gwy_param_def_get_param_name() for parameter names (keys).
 *
 * Returns: The parameter name, as a string owned by @pardef. It may be %NULL if the parameter has no label.
 *
 * Since: 2.62
 **/
const gchar*
gwy_param_def_get_param_label(GwyParamDef *pardef,
                              gint id)
{
    const GwyParamDefItem *item;

    g_return_val_if_fail(GWY_IS_PARAM_DEF(pardef), NULL);
    item = _gwy_param_def_item(pardef, _gwy_param_def_index(pardef, id));
    g_return_val_if_fail(item, NULL);
    return item->desc;
}

static gint
count_enum_values(const GwyEnum *values, gint nvalues)
{
    if (nvalues < 0) {
        nvalues = 0;
        while (values[nvalues].name)
            nvalues++;
    }
    return nvalues;
}

static gint
find_enum_value(const GwyEnum *values, gint nvalues, gint value)
{
    gint i;

    for (i = 0; i < nvalues; i++) {
        if (values[i].value == value)
            return i;
    }
    return -1;
}

/**
 * gwy_param_def_add_gwyenum:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for the list header label in GUI.  May be also %NULL for no label.
 * @values: An array of corresponding string-integer pairs.
 * @nvalues: The number of @nvalues items.  It is possible to pass -1 to count the values automatically if the array
 *           is terminated by %NULL name.
 * @default_value: Default value of the parameter.
 *
 * Defines a new parameter with enumerated values defined by a #GwyEnum.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * See gwy_param_def_add_enum() for predefined enums.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_gwyenum(GwyParamDef *pardef,
                          gint id,
                          const gchar *name,
                          const gchar *desc,
                          const GwyEnum *values,
                          gint nvalues,
                          gint default_value)
{
    gwy_param_def_append_enum(pardef, id, name, desc, 0, values, nvalues, default_value);
}

/**
 * gwy_param_def_add_gwyflags:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for the list header label in GUI.  May be also %NULL for no label.
 * @values: An array of corresponding string-integer pairs.
 * @nvalues: The number of @nvalues items.  It is possible to pass -1 to count the values automatically if the array
 *           is terminated by %NULL name.
 * @default_value: Default value of the parameter.
 *
 * Defines a new parameter with bit flag values defined by a #GwyEnum.
 *
 * The values in @values must represent a set of flags, i.e. numbers with exactly one 1-bit in binary.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_gwyflags(GwyParamDef *pardef,
                           gint id,
                           const gchar *name,
                           const gchar *desc,
                           const GwyEnum *values,
                           gint nvalues,
                           guint default_value)
{
    gwy_param_def_append_flags(pardef, id, name, desc, 0, values, nvalues, default_value);
}

/**
 * gwy_param_def_add_enum:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.  May be %NULL for the default label.
 * @enum_gtype: Type of the enum, for instance %GWY_TYPE_MASKING_TYPE.
 * @default_value: Default value of the parameter.
 *
 * Defines a new parameter with enumerated values from a standard Gwyddion enum.
 *
 * Supported enums include #GwyMaskingType, #GwyInterpolationType, #GwyOrientation, #GwyMergeType,
 * #GwyDistanceTransformType, #GwyWindowingType and GwyGraphCurveType.  They have standard labels allowing to pass
 * %NULL as @desc.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * See gwy_param_def_add_gwyenum() for enums defined by a #GwyEnum table.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_enum(GwyParamDef *pardef,
                       gint id,
                       const gchar *name,
                       const gchar *desc,
                       GType enum_gtype,
                       gint default_value)
{
    static const GwyEnum dummy_enum[] = { { "???", 0 }, { NULL, 0 } };
    const GwyEnum *values = NULL;
    const gchar *standard_desc = NULL;

    g_return_if_fail(enum_gtype);
    if (enum_gtype == GWY_TYPE_MASKING_TYPE) {
        values = gwy_masking_type_get_enum();
        standard_desc = _("_Masking");
    }
    else if (enum_gtype == GWY_TYPE_INTERPOLATION_TYPE) {
        values = gwy_interpolation_type_get_enum();
        standard_desc = _("_Interpolation type");
    }
    else if (enum_gtype == GWY_TYPE_ORIENTATION) {
        values = gwy_orientation_get_enum();
        standard_desc = _("_Direction");
    }
    else if (enum_gtype == GWY_TYPE_MERGE_TYPE) {
        values = gwy_merge_type_get_enum();
        standard_desc = _("Combine with existing mask");
    }
    else if (enum_gtype == GWY_TYPE_WINDOWING_TYPE) {
        values = gwy_windowing_type_get_enum();
        standard_desc = _("_Windowing type");
    }
    else if (enum_gtype == GWY_TYPE_DISTANCE_TRANSFORM_TYPE) {
        values = gwy_distance_transform_type_get_enum();
        standard_desc = _("_Distance type");
    }
    else if (enum_gtype == GWY_TYPE_GRAPH_CURVE_TYPE) {
        values = gwy_graph_curve_type_get_enum();
        standard_desc = _("Plot _style");
    }
    else {
        /* We could play with GEnumClass here but why.  It should be implemented if it is used. */
        standard_desc = g_type_name(enum_gtype);
        g_warning("Enum %s is unimplemented.  Should be?", standard_desc);
        values = dummy_enum;
    }
    gwy_param_def_append_enum(pardef, id, name, desc ? desc : standard_desc, enum_gtype, values, -1, default_value);
}

static void
gwy_param_def_append_enum(GwyParamDef *pardef,
                          gint id,
                          const gchar *name,
                          const gchar *desc,
                          GType gtype,
                          const GwyEnum *values,
                          gint nvalues,
                          gint default_value)
{
    GwyParamDefItem item;
    GwyParamDefEnum *e = &item.def.e;

    if (!nvalues || !values) {
        g_warning("Enum param %s (%s) has no values.", name ? name : "(anonymous)", desc ? desc : "");
        nvalues = 0;
        values = NULL;
    }

    /* XXX: We might want to check that GType values match GwyEnum values.  But as long as the only use of the
     * functions is internal then it is probably unnecessary. */
    item.type = GWY_PARAM_ENUM;
    e->gtype = gtype;
    e->table = values;
    e->nvalues = nvalues = count_enum_values(values, nvalues);
    e->default_value_index = find_enum_value(values, nvalues, default_value);
    if (e->default_value_index < 0) {
        g_warning("Enum param %s (%s) default value %d is not in the enum.",
                  name ? name : "(anonymous)", desc ? desc : "", default_value);
        e->default_value_index = 0;
    }

    gwy_param_def_append(pardef, id, name, desc, &item);
}

static void
gwy_param_def_append_flags(GwyParamDef *pardef,
                           gint id,
                           const gchar *name,
                           const gchar *desc,
                           GType gtype,
                           const GwyEnum *values,
                           gint nvalues,
                           guint default_value)
{
    GwyParamDefItem item;
    GwyParamDefFlags *f = &item.def.f;
    guint i, b, allset;
    gboolean warned = FALSE;

    if (!nvalues || !values) {
        g_warning("Flags param %s (%s) has no values.", name ? name : "(anonymous)", desc ? desc : "");
        nvalues = 0;
        values = NULL;
    }

    /* XXX: We might want to check that GType values match GwyEnum values.  But as long as the only use of the
     * functions is internal then it is probably unnecessary. */
    item.type = GWY_PARAM_FLAGS;
    f->gtype = gtype;
    f->table = values;
    f->nvalues = nvalues = count_enum_values(values, nvalues);
    allset = 0;
    for (i = 0; i < nvalues; i++) {
        b = values[i].value;
        if (b & (b - 1)) {
            /* Can't fix this one. */
            g_warning("Flags param %s (%s) flag %u does not have exactly 1 bit set.",
                      name ? name : "(anonymous)", desc ? desc : "", values[i].value);
            warned = TRUE;
        }
        allset |= b;
    }
    f->allset = allset;
    if (!warned) {
        /* Count set bits (Brian Kernighan's). */
        b = 0;
        while (allset) {
            allset &= allset - 1;
            b++;
        }
        if (b != nvalues) {
            /* Can't fix this one. */
            g_warning("Flags param %s (%s) value bits are not independent.",
                      name ? name : "(anonymous)", desc ? desc : "");
        }
    }
    f->default_value = _gwy_param_def_rectify_flags(&item, default_value);
    if (f->default_value != default_value) {
        g_warning("Flags param %s (%s) default value %u has bits not among the flags.",
                  name ? name : "(anonymous)", desc ? desc : "", default_value);
    }

    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_int:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.
 * @minimum: Minimum allowed value (inclusive).
 * @maximum: Maximum allowed value (inclusive).
 * @default_value: Default value of the parameter.
 *
 * Defines a new parameter with integer values.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_int(GwyParamDef *pardef,
                      gint id,
                      const gchar *name,
                      const gchar *desc,
                      gint minimum,
                      gint maximum,
                      gint default_value)
{
    GwyParamDefItem item;
    GwyParamDefInt *i = &item.def.i;

    if (minimum > maximum) {
        g_warning("Int param %s (%s) has minimum > maximum (%d > %d).",
                  name ? name : "(anonymous)", desc ? desc : "", minimum, maximum);
        GWY_SWAP(gint, minimum, maximum);
    }
    item.type = GWY_PARAM_INT;
    i->minimum = minimum;
    i->maximum = maximum;
    i->default_value = _gwy_param_def_rectify_int(&item, default_value);
    if (i->default_value != default_value) {
        g_warning("Int param %s (%s) default value %d is out of range [%d..%d].",
                  name ? name : "(anonymous)", desc ? desc : "", default_value, minimum, maximum);
    }
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_active_page:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL if not stored in settings, but then the active page parameter
 *        would have no reason to exist.
 * @desc: Parameter description, usually %NULL.
 *
 * Defines a new parameter with integer values representing module dialog active page.
 *
 * The active page parameter can be connected to a #GtkNotebook using the utility function
 * gwy_param_active_page_link_to_notebook().  This is also the only function that should be used with such parameter.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_active_page(GwyParamDef *pardef,
                              gint id,
                              const gchar *name,
                              const gchar *desc)
{
    GwyParamDefItem item;

    item.type = GWY_PARAM_ACTIVE_PAGE;
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_boolean:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.
 * @default_value: Default value of the parameter.
 *
 * Defines a new parameter with boolean values.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_boolean(GwyParamDef *pardef,
                          gint id,
                          const gchar *name,
                          const gchar *desc,
                          gboolean default_value)
{
    GwyParamDefItem item;
    GwyParamDefBoolean *b = &item.def.b;

    b->default_value = !!default_value;
    b->is_instant_updates = FALSE;
    b->seed_id = -1;
    item.type = GWY_PARAM_BOOLEAN;
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_instant_updates:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.  Usually %NULL for the default label.
 * @default_value: Default value of the parameter.
 *
 * Defines a new boolean parameter representing the instant updates option.
 *
 * Compared to a plain boolean, a parameter defined using this function has a predefined label and is recognised by
 * #GwyDialog.  Therefore, it is not necessary to call gwy_dialog_set_instant_updates_param().  If your module GUI
 * needs non-standard update settings then define the parameters manually.
 *
 * Furthermore, it is normally not necessary (in fact, counterproductive) to call gwy_dialog_invalidate() when the
 * instant updates parameter changes.  Recalculation is only necessary when instant updates were just switched on
 * and the current preview is no longer valid.  #GwyDialog handles this case automatically.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_instant_updates(GwyParamDef *pardef,
                                  gint id,
                                  const gchar *name,
                                  const gchar *desc,
                                  gboolean default_value)
{
    GwyParamDefItem item;
    GwyParamDefBoolean *b = &item.def.b;

    b->default_value = !!default_value;
    b->is_instant_updates = TRUE;
    b->seed_id = -1;
    if (!desc)
        desc = _("I_nstant updates");
    item.type = GWY_PARAM_BOOLEAN;
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_randomize:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @seed_id: Identifier of the corresponding random seed parameter.  It must already be defined.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.  Usually %NULL for the default label.
 * @default_value: Default value of the parameter.
 *
 * Defines a new boolean parameter representing the randomize option for a random seed.
 *
 * See gwy_param_def_add_seed() for random seed parameters.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_randomize(GwyParamDef *pardef,
                            gint id,
                            gint seed_id,
                            const gchar *name,
                            const gchar *desc,
                            gboolean default_value)
{
    GwyParamDefPrivate *priv = pardef->priv;
    GwyParamDefItem item;
    GwyParamDefBoolean *b = &item.def.b;
    GwyParamDefRandomSeed *rs;
    gint i;

    item.type = GWY_PARAM_BOOLEAN;
    b->default_value = !!default_value;
    b->is_instant_updates = FALSE;
    b->seed_id = seed_id;

    i = find_param_def(priv, seed_id);
    g_assert(i >= 0);
    rs = &priv->defs[i].def.rs;
    g_assert(rs->randomize_id < 0);
    rs->randomize_id = id;

    if (!desc)
        desc = _("Randomi_ze");
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_double:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.
 * @minimum: Minimum allowed value (inclusive).
 * @maximum: Maximum allowed value (inclusive).
 * @default_value: Default value of the parameter.
 *
 * Defines a new parameter with floating point values.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_double(GwyParamDef *pardef,
                         gint id,
                         const gchar *name,
                         const gchar *desc,
                         gdouble minimum,
                         gdouble maximum,
                         gdouble default_value)
{
    GwyParamDefItem item;
    GwyParamDefDouble *d = &item.def.d;

    gwy_clear(d, 1);
    if (minimum > maximum) {
        g_warning("Double param %s (%s) has minimum > maximum (%.14g > %.14g).",
                  name ? name : "(anonymous)", desc ? desc : "", minimum, maximum);
        GWY_SWAP(gdouble, minimum, maximum);
    }
    item.type = GWY_PARAM_DOUBLE;
    d->minimum = minimum;
    d->maximum = maximum;
    d->default_value = _gwy_param_def_rectify_double(&item, default_value);
    if (d->default_value != default_value) {
        g_warning("Double param %s (%s) default value %.14g is out of range [%.14g..%.14g].",
                  name ? name : "(anonymous)", desc ? desc : "", default_value, minimum, maximum);
    }
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_angle:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.
 * @positive: %TRUE for positive angles, %FALSE for angles symmetrical around zero.
 * @folding: Fraction of the range [0,2π] which should be the range.
 * @default_value: Default value of the parameter.
 *
 * Defines a new parameter with floating point values representing angles.
 *
 * Percentages do not have to be defined using this function.  You can also set up the conversion factor and unit
 * string directly using gwy_param_table_slider_set_factor() and gwy_param_table_set_unitstr() when constructing the
 * parameter table (this happens automatically for percentage parameters).  This function may be somewhat more
 * convenient and it also ensures correct folding when loading angle from settings.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_angle(GwyParamDef *pardef,
                        gint id,
                        const gchar *name,
                        const gchar *desc,
                        gboolean positive,
                        gint folding,
                        gdouble default_value)
{
    GwyParamDefItem item;
    GwyParamDefDouble *d = &item.def.d;

    gwy_clear(d, 1);
    item.type = GWY_PARAM_DOUBLE;
    if (!folding || folding > 12) {
        g_warning("Wrong folding value %d.", folding);
        folding = 1;
    }
    if (positive) {
        d->minimum = 0.0;
        d->maximum = 2.0/folding*G_PI;
    }
    else {
        d->maximum = G_PI/folding;
        d->minimum = -d->maximum;
    }
    d->is_angle = TRUE;
    d->angle_positive = positive;
    d->angle_folding = folding;
    d->default_value = _gwy_param_def_rectify_double(&item, default_value);
    if (d->default_value != default_value) {
        g_warning("Double param %s (%s) default value %.14g is out of range [%.14g..%.14g].",
                  name ? name : "(anonymous)", desc ? desc : "", default_value, d->minimum, d->maximum);
    }
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_percentage:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.
 * @default_value: Default value of the parameter.
 *
 * Defines a new parameter with floating point values representing fraction of some base value.
 *
 * The value range is [0,1], but it is displayed in percents in the GUI.
 *
 * Percentages do not have to be defined using this function.  You can also set up the conversion factor and unit
 * string directly using gwy_param_table_slider_set_factor() and gwy_param_table_set_unitstr() when constructing the
 * parameter table (this happens automatically for percentage parameters).  This function may be somewhat more
 * convenient.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_percentage(GwyParamDef *pardef,
                             gint id,
                             const gchar *name,
                             const gchar *desc,
                             gdouble default_value)
{
    GwyParamDefItem item;
    GwyParamDefDouble *d = &item.def.d;

    gwy_clear(d, 1);
    item.type = GWY_PARAM_DOUBLE;
    d->is_percentage = TRUE;
    d->minimum = 0.0;
    d->maximum = 1.0;
    d->default_value = _gwy_param_def_rectify_double(&item, default_value);
    if (d->default_value != default_value) {
        g_warning("Double param %s (%s) default value %.14g is out of range [%.14g..%.14g].",
                  name ? name : "(anonymous)", desc ? desc : "", default_value, d->minimum, d->maximum);
    }
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_mask_color:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  Usually it is %NULL because the colour is taken from data, not settings.
 * @desc: Parameter description which will be used for a label in GUI.  Usually %NULL for the default label.
 *
 * Defines a new mask colour parameter.
 *
 * This is usually a parameter not stored in settings, but handled specially by taking the mask colour from the image
 * and then setting mask colours on the output.  The default value is the default mask colour.
 *
 * See gwy_param_def_add_color() for a generic colour parameter.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_mask_color(GwyParamDef *pardef,
                             gint id,
                             const gchar *name,
                             const gchar *desc)
{
    /* Use a fixed colour.  ‘The current mask colour at the time the parameter was defined’ does not work as a good
     * default value.  The default colour must be handled properly later. */
    const GwyRGBA default_mask_color = { 1.0, 0.0, 0.0, 0.5 };

    GwyParamDefItem item;
    GwyParamDefColor *c = &item.def.c;

    if (!desc)
        desc = _("_Mask color");
    item.type = GWY_PARAM_COLOR;
    c->is_mask = TRUE;
    c->has_alpha = TRUE;
    c->default_value = default_mask_color;
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_color:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  Usually it is %NULL because the colour is taken from data, not settings.
 * @desc: Parameter description which will be used for a label in GUI.  Usually %NULL for the default label.
 * @has_alpha: %TRUE if semi-transparent colours are allowed; %FALSE for fully opaque colours.
 * @default_value: Default value of the parameter.
 *
 * Defines a new colour parameter.
 *
 * See gwy_param_def_add_mask_color() for the mask colour parameter which is handled specially.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.64
 **/
void
gwy_param_def_add_color(GwyParamDef *pardef,
                        gint id,
                        const gchar *name,
                        const gchar *desc,
                        gboolean has_alpha,
                        GwyRGBA default_value)
{
    GwyParamDefItem item;
    GwyParamDefColor *c = &item.def.c;

    if (!desc)
        desc = _("Colo_r");
    item.type = GWY_PARAM_COLOR;
    c->is_mask = FALSE;
    c->has_alpha = has_alpha;
    c->default_value = _gwy_param_def_rectify_color(&item, default_value);
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_target_graph:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.  May be %NULL for the default label.
 *
 * Defines a new parameter with values that are graph ids.
 *
 * Data identifier parameters are not stored permanently in settings.  However, they are remembered until Gwyddion
 * exits.  Hence @name does not actually refer to settings, but it should still uniquely identify the parameter as in
 * settings.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * See gwy_param_def_add_target_graph() for a generic graph parameter.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_target_graph(GwyParamDef *pardef,
                               gint id,
                               const gchar *name,
                               const gchar *desc)
{
    GwyParamDefItem item;
    GwyParamDefDataId *di = &item.def.di;

    item.type = GWY_PARAM_GRAPH_ID;
    di->is_target_graph = TRUE;
    gwy_param_def_append(pardef, id, name, desc ? desc : _("Target _graph"), &item);
}

/**
 * gwy_param_def_add_graph_id:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.
 *
 * Defines a new parameter with values that are graph ids.
 *
 * Data identifier parameters are not stored permanently in settings.  However, they are remembered until Gwyddion
 * exits.  Hence @name does not actually refer to settings, but it should still uniquely identify the parameter as in
 * settings.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * See gwy_param_def_add_target_graph() for a specialised parameter for target graphs.
 *
 * Since: 2.60
 **/
void
gwy_param_def_add_graph_id(GwyParamDef *pardef,
                           gint id,
                           const gchar *name,
                           const gchar *desc)
{
    GwyParamDefItem item;
    GwyParamDefDataId *di = &item.def.di;

    item.type = GWY_PARAM_GRAPH_ID;
    di->is_target_graph = FALSE;
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_image_id:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.
 *
 * Defines a new parameter with values that are image ids.
 *
 * Data identifier parameters are not stored permanently in settings.  However, they are remembered until Gwyddion
 * exits.  Hence @name does not actually refer to settings, but it should still uniquely identify the parameter as in
 * settings.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_image_id(GwyParamDef *pardef,
                           gint id,
                           const gchar *name,
                           const gchar *desc)
{
    GwyParamDefItem item;

    item.type = GWY_PARAM_IMAGE_ID;
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_volume_id:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.
 *
 * Defines a new parameter with values that are volume data ids.
 *
 * Data identifier parameters are not stored permanently in settings.  However, they are remembered until Gwyddion
 * exits.  Hence @name does not actually refer to settings, but it should still uniquely identify the parameter as in
 * settings.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_volume_id(GwyParamDef *pardef,
                            gint id,
                            const gchar *name,
                            const gchar *desc)
{
    GwyParamDefItem item;

    item.type = GWY_PARAM_VOLUME_ID;
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_xyz_id:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.
 *
 * Defines a new parameter with values that are xyz data ids.
 *
 * Data identifier parameters are not stored permanently in settings.  However, they are remembered until Gwyddion
 * exits.  Hence @name does not actually refer to settings, but it should still uniquely identify the parameter as in
 * settings.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_xyz_id(GwyParamDef *pardef,
                         gint id,
                         const gchar *name,
                         const gchar *desc)
{
    GwyParamDefItem item;

    item.type = GWY_PARAM_XYZ_ID;
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_curve_map_id:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.
 *
 * Defines a new parameter with values that are curve map data ids.
 *
 * Data identifier parameters are not stored permanently in settings.  However, they are remembered until Gwyddion
 * exits.  Hence @name does not actually refer to settings, but it should still uniquely identify the parameter as in
 * settings.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.60
 **/
void
gwy_param_def_add_curve_map_id(GwyParamDef *pardef,
                               gint id,
                               const gchar *name,
                               const gchar *desc)
{
    GwyParamDefItem item;

    item.type = GWY_PARAM_CURVE_MAP_ID;
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_graph_curve:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.
 * @desc: Parameter description which will be used for a label in GUI.  It may be %NULL for the default label.
 *
 * Defines a new parameter with values that are graph curve numbers.
 *
 * Curve numbers can be any non-negative integers.  The upper bound is determined by #GwyParamTable once the GUI for
 * choosing a curve from specific graph model is set up.  Value -1 is also allowed for empty graph models with no
 * curves, but this should not normally occur.
 *
 * Curve parameters are stored in settings as curve labels, even though they are treated integers otherwise.  This
 * helps selecting a curve with the same function/interpretation the next time the module is invoked – or the first
 * curve if there is none such.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.60
 **/
void
gwy_param_def_add_graph_curve(GwyParamDef *pardef,
                              gint id,
                              const gchar *name,
                              const gchar *desc)
{
    GwyParamDefItem item;
    GwyParamDefInt *i = &item.def.i;

    item.type = GWY_PARAM_GRAPH_CURVE;
    i->minimum = -1;
    i->maximum = G_MAXINT;
    i->default_value = 0;
    if (!desc)
        desc = _("C_urve");
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_lawn_curve:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.
 * @desc: Parameter description which will be used for a label in GUI.  It may be %NULL for the default label.
 *
 * Defines a new parameter with values that are lawn curve numbers.
 *
 * Curve numbers can be any non-negative integers.  The upper bound is determined by #GwyParamTable once the GUI for
 * choosing a curve from specific lawn is set up.
 *
 * Curve parameters are stored in settings as curve labels, even though they are treated integers otherwise.  This
 * helps selecting a curve with the same function/interpretation the next time the module is invoked – or the first
 * curve if there is none such.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.60
 **/
void
gwy_param_def_add_lawn_curve(GwyParamDef *pardef,
                             gint id,
                             const gchar *name,
                             const gchar *desc)
{
    GwyParamDefItem item;
    GwyParamDefInt *i = &item.def.i;

    item.type = GWY_PARAM_LAWN_CURVE;
    i->minimum = 0;
    i->maximum = G_MAXINT;
    i->default_value = 0;
    if (!desc)
        desc = _("C_urve");
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_lawn_segment:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.
 * @desc: Parameter description which will be used for a label in GUI.  It may be %NULL for the default label.
 *
 * Defines a new parameter with values that are lawn segment numbers.
 *
 * Segment numbers can be any non-negative integers.  The upper bound is determined by #GwyParamTable once the GUI for
 * choosing a segment from specific lawn is set up.  Value -1 is also allowed for empty lawns with no segments.
 *
 * Segment parameters are stored in settings as segment labels, even though they are treated integers otherwise.  This
 * helps selecting a segment with the same function/interpretation the next time the module is invoked – or the first
 * segment if there is none such.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.60
 **/
void
gwy_param_def_add_lawn_segment(GwyParamDef *pardef,
                               gint id,
                               const gchar *name,
                               const gchar *desc)
{
    GwyParamDefItem item;
    GwyParamDefInt *i = &item.def.i;

    item.type = GWY_PARAM_LAWN_SEGMENT;
    i->minimum = -1;
    i->maximum = G_MAXINT;
    i->default_value = 0;
    if (!desc)
        desc = _("_Segment");
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_report_type:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.  The label, if not %NULL, will be used for the
 *        save dialogue as the export controls carry no label.
 * @style: Report style, defining allowed report types.  See gwy_results_export_set_style() for description.
 * @default_value: Default value of the parameter.
 *
 * Defines a new parameter with values that are report types.
 *
 * Report type values are neither pure enumerated values nor flags; they are combinations of both.  See
 * #GwyResultsExport for discussion.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_report_type(GwyParamDef *pardef,
                              gint id,
                              const gchar *name,
                              const gchar *desc,
                              GwyResultsExportStyle style,
                              GwyResultsReportType default_value)
{
    GwyParamDefItem item;
    GwyParamDefReportType *rt = &item.def.rt;

    gwy_clear(rt, 1);
    item.type = GWY_PARAM_REPORT_TYPE;
    if (style != GWY_RESULTS_EXPORT_PARAMETERS
        && style != GWY_RESULTS_EXPORT_TABULAR_DATA
        && style != GWY_RESULTS_EXPORT_FIXED_FORMAT) {
        g_warning("Report type param %s (%s) has invalid style (%u).",
                  name ? name : "(anonymous)", desc ? desc : "", style);
        style = GWY_RESULTS_EXPORT_PARAMETERS;
    }
    rt->style = style;
    rt->default_value = _gwy_param_def_rectify_report_type(&item, default_value);
    if (rt->default_value != default_value) {
        g_warning("Report type param %s (%s) default value %u is not among allowed values.",
                  name ? name : "(anonymous)", desc ? desc : "", default_value);
    }
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_hold_selection:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.  Normally pass %NULL for the default label.
 *
 * Defines a new flags parameter with values that are selection holding flags.
 *
 * The values are flags from #GwyHoldSelectionFlags. The default is zero.
 *
 * Since: 2.63
 **/
void
gwy_param_def_add_hold_selection(GwyParamDef *pardef,
                                 gint id,
                                 const gchar *name,
                                 const gchar *desc)
{
    static const GwyEnum hold_flags[] = {
        { N_("Hold selection"),                            GWY_HOLD_SELECTION_HOLD,      },
        { N_("Replace existing selection"),                GWY_HOLD_SELECTION_OVERWRITE, },
        { N_("Replace even with empty (clear selection)"), GWY_HOLD_SELECTION_CLEAR,     },
    };

    if (!desc)
        desc = _("_Hold selection");
    gwy_param_def_append_flags(pardef, id, name, desc, GWY_TYPE_HOLD_SELECTION_FLAGS,
                               hold_flags, G_N_ELEMENTS(hold_flags), 0);
}

/**
 * gwy_param_def_add_seed:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.  Normally pass %NULL for the default label.
 *
 * Defines a new parameter with values that are random seeds.
 *
 * Random seeds are integers from the range [1,2³¹-1] which can take random values. They are usually accompanied by
 * a boolean parameter controlling whether the value is random or fixed (taken from settings), added afterwards using
 * gwy_param_def_add_randomize().
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_seed(GwyParamDef *pardef,
                       gint id,
                       const gchar *name,
                       const gchar *desc)
{
    GwyParamDefItem item;
    GwyParamDefRandomSeed *rs = &item.def.rs;

    item.type = GWY_PARAM_RANDOM_SEED;
    rs->randomize_id = -1;
    if (!desc)
        desc = _("R_andom seed");
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_string:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.
 * @flags: Simple treatment of special values.  If any are given they are enforced, i.e. they apply both before and
 *         after @rectify in the case both are given.
 * @rectify: Function which takes a string which may or may not be valid parameter value and returns a valid one.
 *           Pass %NULL if anything goes (or validation is handled otherwise).
 * @default_value: Default value of the parameter.
 *
 * Defines a new parameter with values that are strings.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_string(GwyParamDef *pardef,
                         gint id,
                         const gchar *name,
                         const gchar *desc,
                         GwyParamStringFlags flags,
                         GwyRectifyStringFunc rectify,
                         const gchar *default_value)
{
    GwyParamDefItem item;
    GwyParamDefString *s = &item.def.s;

    item.type = GWY_PARAM_STRING;
    s->flags = flags;
    s->rectify = rectify;
    s->default_value = _gwy_param_def_rectify_string(&item, default_value);
    if (g_strcmp0(s->default_value, default_value)) {
        g_warning("String param %s (%s) default value \"%s\" does not rectify to itself but to \"%s\".",
                  name ? name : "(anonymous)", desc ? desc : "", default_value, s->default_value);
    }
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_unit:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.
 * @default_value: Default value of the parameter.  %NULL is allowed.
 *
 * Defines a new parameter with values that are strings representing units.
 *
 * Units parameters are not enforced to parse to meaningful units.  However, they are conceptually different from
 * arbitrary strings and have several special unit-oriented functions.
 *
 * Units behave as strings with the %GWY_PARAM_STRING_EMPTY_IS_NULL flag.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_unit(GwyParamDef *pardef,
                       gint id,
                       const gchar *name,
                       const gchar *desc,
                       const gchar *default_value)
{
    GwyParamDefItem item;
    GwyParamDefUnit *si = &item.def.si;

    item.type = GWY_PARAM_UNIT;
    si->default_value = g_intern_string(default_value);
    gwy_param_def_append(pardef, id, name, desc, &item);
}

/**
 * gwy_param_def_add_resource:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which will be used for a label in GUI.
 * @inventory: Inventory holding the resources to choose from.
 * @default_value: Default value of the parameter (resource name).
 *
 * Defines a new parameter with values that are string resource names.
 *
 * Use gwy_params_get_string() to get the string name and gwy_params_get_resource() to get the corresponding resource
 * object.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.59
 **/
void
gwy_param_def_add_resource(GwyParamDef *pardef,
                           gint id,
                           const gchar *name,
                           const gchar *desc,
                           GwyInventory *inventory,
                           const gchar *default_value)
{
    GwyParamDefItem item;
    GwyParamDefResource *res = &item.def.res;

    item.type = GWY_PARAM_RESOURCE_NAME;
    g_assert(GWY_IS_INVENTORY(inventory));
    res->inventory = inventory;
    res->default_value = g_intern_string(default_value);
    if (!gwy_inventory_get_item(inventory, res->default_value)) {
        g_warning("Resource param %s (%s) default value \"%s\" is not in the inventory.",
                  name ? name : "(anonymous)", desc ? desc : "", default_value);
    }
    gwy_param_def_append(pardef, id, name, desc, &item);
}

static gpointer
init_grain_value_flags(gpointer user_data)
{
    GwyEnum *values = (GwyEnum*)user_data;
    guint i;

    for (i = 0; values[i].value != -1; i++) {
        values[i].name = gwy_grain_value_group_name(values[i].value);
        values[i].value = 1u << values[i].value;
    }
    return values;
}

/**
 * gwy_param_def_add_grain_groups:
 * @pardef: A set of parameter definitions.
 * @id: Parameter identifier.
 * @name: Parameter name for settings.  It can be %NULL for derived parameters not stored in settings.
 * @desc: Parameter description which could be used for a label in GUI, but usually %NULL.
 * @default_value: Default value of the parameter (bits corresponding to grain value groups).
 *
 * Defines a new flag parameter with values that are bits corresponding to grain value groups.
 *
 * Such parameter is usually used to represent the set of expanded grain value groups in a tree view.
 *
 * See the introduction for @name and @desc lifetime rules.
 *
 * Since: 2.61
 **/
void
gwy_param_def_add_grain_groups(GwyParamDef *pardef,
                               gint id,
                               const gchar *name,
                               const gchar *desc,
                               guint default_value)
{
    static GwyEnum values[] = {
        { NULL, GWY_GRAIN_VALUE_GROUP_ID,        },
        { NULL, GWY_GRAIN_VALUE_GROUP_POSITION,  },
        { NULL, GWY_GRAIN_VALUE_GROUP_VALUE,     },
        { NULL, GWY_GRAIN_VALUE_GROUP_AREA,      },
        { NULL, GWY_GRAIN_VALUE_GROUP_VOLUME,    },
        { NULL, GWY_GRAIN_VALUE_GROUP_BOUNDARY,  },
        { NULL, GWY_GRAIN_VALUE_GROUP_SLOPE,     },
        { NULL, GWY_GRAIN_VALUE_GROUP_CURVATURE, },
        { NULL, GWY_GRAIN_VALUE_GROUP_MOMENT,    },
        { NULL, GWY_GRAIN_VALUE_GROUP_USER,      },
        { NULL, -1,                              },
    };
    static GOnce once = G_ONCE_INIT;

    g_once(&once, init_grain_value_flags, values);

    if (!desc)
        desc = _("Expanded groups");
    gwy_param_def_append_flags(pardef, id, name, desc, 0, values, G_N_ELEMENTS(values)-1, default_value);
}

static void
gwy_param_def_append(GwyParamDef *pardef,
                     gint id,
                     const gchar *name,
                     const gchar *desc,
                     GwyParamDefItem *item)
{
    GwyParamDefPrivate *priv;
    gint i, len;

    g_return_if_fail(GWY_IS_PARAM_DEF(pardef));

    priv = pardef->priv;
    if (priv->is_used) {
        g_critical("Parameter definitions can only be modified during construction.");
        return;
    }
    if (id <= priv->maxid && (i = find_param_def(priv, id)) >= 0) {
        g_critical("Item with id %d already exists.", id);
        return;
    }
    if (desc && (len = strlen(desc)) && desc[len-1] == ':') {
        gchar *s;

        g_warning("Parameter description (%s) should not have trailing colons.", desc);
        s = g_strndup(desc, len-1);
        g_strstrip(s);
        desc = g_intern_string(s);
        g_free(s);
    }
    item->id = id;
    item->name = name;
    item->desc = desc;

    if (priv->ndefs == priv->nalloc) {
        priv->nalloc = MAX(2*priv->nalloc, priv->nalloc + 8);
        priv->defs = g_renew(GwyParamDefItem, priv->defs, priv->nalloc);
        priv->ids = g_renew(gint, priv->ids, priv->nalloc);
    }
    priv->defs[priv->ndefs] = *item;
    priv->ids[priv->ndefs] = id;
    priv->ndefs++;
    priv->maxid = MAX(priv->maxid, id);
}

static gint
find_param_def(GwyParamDefPrivate *priv, gint id)
{
    gint *ids = priv->ids;
    gint a, b, c, n = priv->ndefs;

    a = 0;
    b = n-1;
    if (id == ids[a])
        return a;
    if (id == ids[b])
        return b;
    if (id < ids[a] || id > ids[b])
        return -1;

    while (b - a > 1) {
        c = (a + b)/2;
        if (id == ids[c])
            return c;
        if (id < ids[c])
            b = c;
        else
            a = c;
    }

    return -1;
}

#ifdef DEBUG_USERS
static void
remove_current_user(gpointer user_data, GObject *where_the_object_was)
{
    GwyParamDef *pardef = (GwyParamDef*)user_data;

    pardef->priv->users = g_slist_remove(pardef->priv->users, where_the_object_was);
}
#endif

static int
compare_int(const void *pa, const void *pb)
{
    int a = *(const int*)pa;
    int b = *(const int*)pb;

    if (a < b)
        return -1;
    if (a > b)
        return 1;
    return 0;
}

/* Sort ids[] and defs[] in sync by ascending id. */
static void
sort_definitions(GwyParamDefPrivate *priv)
{
    GwyParamDefItem *newdefs;
    guint i, n = priv->ndefs;
    gint *idx = g_new(gint, 2*n);

    for (i = 0; i < n; i++) {
        idx[2*i] = priv->ids[i];
        idx[2*i + 1] = i;
    }

    qsort(idx, n, 2*sizeof(gint), compare_int);

    newdefs = g_new(GwyParamDefItem, n);
    for (i = 0; i < n; i++) {
        priv->ids[i] = idx[2*i];
        newdefs[i] = priv->defs[idx[2*i + 1]];
    }
    g_free(idx);

    GWY_SWAP(GwyParamDefItem*, priv->defs, newdefs);
    g_free(newdefs);
    priv->nalloc = n;
}

void
_gwy_param_def_use(GwyParamDef *pardef, GwyParams *params)
{
    GwyParamDefPrivate *priv = pardef->priv;

    sort_definitions(priv);
    priv->is_used = TRUE;
#ifdef DEBUG_USERS
    /* When presets are implemented using GwyParams then multiple GwyParams instances created for one GwyParamDef
     * are common.  Keep enabled this check during development but we may want a mechanism to exclude presets. */
    if (!g_slist_find(priv->users, params)) {
        /* XXX: Hardcoded warning suppression for known modules with GwyParamResource presets and internal
         * parameter set duplication. */
        if (priv->users && !gwy_stramong(priv->function_name,
                                         "rawfile", "imgexport", "pixmap",
                                         "terracefit", "graph_terraces", "logistic",
                                         "volume_psf",
                                         NULL)) {
            g_warning("Parameter definitions for %s are used multiple times.  "
                      "Check module function %s; it is probably leaking GwyParams objects!",
                      priv->function_name, priv->function_name);
        }
        priv->users = g_slist_prepend(priv->users, params);
        g_object_weak_ref(G_OBJECT(params), remove_current_user, pardef);
    }
#endif
}

guint
_gwy_param_def_size(GwyParamDef *pardef)
{
    return pardef->priv->ndefs;
}

gint
_gwy_param_def_index(GwyParamDef *pardef, gint id)
{
    return find_param_def(pardef->priv, id);
}

/* Return NULL for negative i to make safe _gwy_param_def_item(pardef, _gwy_param_def_index(pardef, id)) */
const GwyParamDefItem*
_gwy_param_def_item(GwyParamDef *pardef, gint i)
{
    return (i >= 0 ? pardef->priv->defs + i : NULL);
}

gint
_gwy_param_def_rectify_enum(const GwyParamDefItem *def, gint value)
{
    const GwyParamDefEnum *e;
    gint i;

    g_return_val_if_fail(def && def->type == GWY_PARAM_ENUM, 0);
    e = &def->def.e;
    i = find_enum_value(e->table, e->nvalues, value);
    return (i >= 0 ? value : e->table[e->default_value_index].value);
}

guint
_gwy_param_def_rectify_flags(const GwyParamDefItem *def, guint value)
{
    const GwyParamDefFlags *f;

    g_return_val_if_fail(def && def->type == GWY_PARAM_FLAGS, 0);
    f = &def->def.f;
    return value & f->allset;
}

gint
_gwy_param_def_rectify_int(const GwyParamDefItem *def, gint value)
{
    const GwyParamDefInt *i;

    g_return_val_if_fail(def, 0);
    if (def->type == GWY_PARAM_ACTIVE_PAGE)
        return value;
    g_return_val_if_fail(def->type == GWY_PARAM_INT || param_type_is_curve_no(def->type), 0);
    i = &def->def.i;
    if ((value < 0 && i->minimum >= 0) || (value > 0 && i->maximum <= 0))
        value = -value;
    return CLAMP(value, i->minimum, i->maximum);
}

gdouble
_gwy_param_def_rectify_double(const GwyParamDefItem *def, gdouble value)
{
    const GwyParamDefDouble *d;

    g_return_val_if_fail(def && def->type == GWY_PARAM_DOUBLE, 0.0);
    d = &def->def.d;
    if (d->is_angle) {
        value = fmod(value, 2.0/d->angle_folding*G_PI);
        /* fmod() rounds to zero, so we now have range (-2π/f,2π/f). */
        if (d->angle_positive) {
            if (value < 0.0)
                value += 2.0/d->angle_folding*G_PI;
        }
        else {
            if (value > d->maximum)
                value -= d->maximum;
            else if (value < d->minimum)
                value += d->maximum;
        }
    }
    else {
        if ((value < 0.0 && d->minimum >= 0.0) || (value > 0.0 && d->maximum <= 0.0))
            value = -value;
    }
    return CLAMP(value, d->minimum, d->maximum);
}

GwyRGBA
_gwy_param_def_rectify_color(const GwyParamDefItem *def,
                             GwyRGBA value)
{
    GwyRGBA color = gwy_param_fallback_color;
    const GwyParamDefColor *c;

    g_return_val_if_fail(def && def->type == GWY_PARAM_COLOR, color);
    c = &def->def.c;
    color.r = CLAMP(value.r, 0.0, 1.0);
    color.g = CLAMP(value.g, 0.0, 1.0);
    color.b = CLAMP(value.b, 0.0, 1.0);
    color.a = (c->has_alpha ? CLAMP(value.a, 0.0, 1.0) : 1.0);
    return color;
}

GwyResultsReportType
_gwy_param_def_rectify_report_type(const GwyParamDefItem *def, GwyResultsReportType value)
{
    const GwyParamDefReportType *rt;
    GwyResultsReportType base_type, flags;

    g_return_val_if_fail(def && def->type == GWY_PARAM_REPORT_TYPE,
                         GWY_RESULTS_REPORT_COLON | GWY_RESULTS_REPORT_MACHINE);
    flags = (value & GWY_RESULTS_REPORT_MACHINE);
    base_type = (value & 0x3);
    base_type = CLAMP(base_type, GWY_RESULTS_REPORT_COLON, GWY_RESULTS_REPORT_CSV);
    rt = &def->def.rt;
    if (rt->style == GWY_RESULTS_EXPORT_TABULAR_DATA && base_type == GWY_RESULTS_REPORT_COLON)
        base_type = GWY_RESULTS_REPORT_TABSEP;
    return base_type | flags;
}

gint
_gwy_param_def_rectify_random_seed(const GwyParamDefItem *def, gint value)
{
    G_GNUC_UNUSED const GwyParamDefRandomSeed *rs;

    g_return_val_if_fail(def && def->type == GWY_PARAM_RANDOM_SEED, 42);
    rs = &def->def.rs;
    return CLAMP(value, 1, 0x7fffffff);
}

gchar*
_gwy_param_def_rectify_string(const GwyParamDefItem *def, const gchar *value)
{
    const GwyParamDefString *s;

    if (!def || def->type != GWY_PARAM_STRING) {
        g_assert(def && def->type == GWY_PARAM_STRING);
        return g_strdup("");
    }
    s = &def->def.s;

    return rectify_string(value, s->flags, s->rectify);
}

gchar*
_gwy_param_def_rectify_unit(const GwyParamDefItem *def, const gchar *value)
{
    if (!def || def->type != GWY_PARAM_UNIT) {
        g_assert(def && def->type == GWY_PARAM_UNIT);
        return NULL;
    }
    return rectify_string(value, GWY_PARAM_STRING_EMPTY_IS_NULL, NULL);
}

static gchar*
rectify_string(const gchar *value, GwyParamStringFlags flags, GwyRectifyStringFunc rectify)
{
    gchar *newvalue, *tmpvalue = NULL;

    if ((flags & GWY_PARAM_STRING_NULL_IS_EMPTY) && !value)
        value = "";
    else if ((flags & GWY_PARAM_STRING_EMPTY_IS_NULL) && value && !*value)
        value = NULL;
    if (!(flags & GWY_PARAM_STRING_DO_NOT_STRIP) && value && *value) {
        if (g_ascii_isspace(value[0]) || g_ascii_isspace(value[strlen(value)-1]))
            value = tmpvalue = g_strstrip(g_strdup(value));
    }

    if (rectify) {
        newvalue = rectify(value);
        if ((flags & GWY_PARAM_STRING_NULL_IS_EMPTY) && !newvalue)
            newvalue = g_strdup("");
        else if ((flags & GWY_PARAM_STRING_EMPTY_IS_NULL) && newvalue && !*newvalue) {
            g_free(newvalue);
            newvalue = NULL;
        }
        if (!(flags & GWY_PARAM_STRING_DO_NOT_STRIP) && newvalue)
            g_strstrip(newvalue);
    }
    else {
        if (tmpvalue) {
            newvalue = tmpvalue;
            tmpvalue = NULL;
        }
        else
            newvalue = g_strdup(value);
    }

    g_free(tmpvalue);

    return newvalue;
}

const gchar*
_gwy_param_def_rectify_resource(const GwyParamDefItem *def, const gchar *value)
{
    const GwyParamDefResource *res;
    gpointer item;

    if (!def || def->type != GWY_PARAM_RESOURCE_NAME) {
        g_assert(def && def->type == GWY_PARAM_RESOURCE_NAME);
        return NULL;
    }
    res = &def->def.res;

    item = gwy_inventory_get_item_or_default(res->inventory, value);
    return item ? gwy_resource_get_name(GWY_RESOURCE(item)) : NULL;
}

/**
 * SECTION:param-def
 * @title: GwyParamDef
 * @short_description: Module parameter definitions
 *
 * #GwyParamDef represents a set of module parameter definitions.  Once constructed, it is an immutable object which
 * modules generally keep around (as a static variable) and use it to fetch parameters from settings as #GwyParams.
 *
 * Parameters are idenfitied by integers which must be unique within one set of definitions.  The integers are not
 * public interface.  They only exist within the module and can change between module versions.  In fact, you can
 * assign them run-time (and differently each time Gwyddion is run) if you need.  However, the usual way to define the
 * parameters is using an enum:
 * |[
 * enum {
 *     PARAM_DEGREE,
 *     PARAM_MASKING,
 *     PARAM_UPDATE,
 * };
 * ]|
 * and the module then refers to the parameters using the enum values.  The corresponding parameter definition code
 * could be as follows.  Notice specialised parameter types exists for some common settings, such as instant updates:
 * |[
 * static GwyParamDef*
 * define_module_params()
 * {
 *     static GwyParamDef *paramdef = NULL;
 *
 *     if (paramdef)
 *         return paramdef;
 *
 *     paramdef = gwy_param_def_new();
 *     gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
 *     gwy_param_def_add_int(paramdef, PARAM_DEGREE, "degree", _("_Degree"), 0, 12, 3);
 *     gwy_param_def_add_gwyenum(paramdef, PARAM_MASKING, "masking", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);
 *     gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, FALSE);
 *     return paramdef;
 * }
 * ]|
 * Functions such as g_once() and g_once_init_enter() can also be used to ensure a single initialisation, although it
 * is probably overkill here.
 *
 * In all the parameter definition constructors strings @name and @desc are not copied.  They must exist for the
 * entire lifetime of the @pardef objects.  Usually they are static strings.  In uncommon scenarios the caller must
 * ensure their existence.  Alternatively, g_intern_string() can be used to make semantically static versions of the
 * strings.
 **/

/**
 * GwyParamDef:
 *
 * Object representing a set of parameter definitions.
 *
 * The #GwyParamDef struct contains no public fields.
 *
 * Since: 2.59
 **/

/**
 * GwyParamDefClass:
 *
 * Class of parameter definition sets.
 *
 * Since: 2.59
 **/

/**
 * GwyParamStringFlags:
 * @GWY_PARAM_STRING_EMPTY_IS_NULL: Fold empty string to %NULL.
 * @GWY_PARAM_STRING_NULL_IS_EMPTY: Ensure strings are non-empty by replacing %NULL with an empty string.
 * @GWY_PARAM_STRING_DO_NOT_STRIP: Preserve whitespace at the beginning and end of the string.
 *
 * Flags that can be used when defining a string parameter.
 *
 * Since: 2.59
 **/

/**
 * GwyRectifyStringFunc:
 * @s: Input string.
 *
 * Type of function returning a valid string, given possibly invalid input.
 *
 * Either the input and output can be %NULL (if the string definition allows it).
 *
 * Returns: Newly allocated string, presumably corrected.
 *
 * Since: 2.59
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
