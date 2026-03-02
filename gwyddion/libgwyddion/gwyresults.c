/*
 *  $Id: gwyresults.c 25196 2023-01-10 12:59:20Z yeti-dn $
 *  Copyright (C) 2017-2022 David Necas (Yeti).
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

#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "gwymacros.h"
#include "gwymath.h"
#include "gwyutils.h"
#include "gwynlfit.h"
#include "gwyresults.h"

/* Lower symmetric part indexing */
/* i MUST be greater or equal than j */
#define SLi(a, i, j) a[(i)*((i) + 1)/2 + (j)]

typedef struct {
    guint pos;
    guint len;
    guint width;
} CovarMatrixSymbol;

typedef union {
    gdouble d;
    glong i;
    gchar *s;
    gboolean yn;
} ResultValueAny;

typedef struct {
    ResultValueAny value;
    /* Position, length of %{name}c the format string */
    guint fbegin;
    guint flen;
    gboolean is_set;
} ResultValue;

typedef struct {
    guint n;
    guint size;
    ResultValue *values;
} ValueSet;

typedef struct {
    GwySIValueFormat *vf;
    gdouble max;
    guint nmembers;
    gboolean seen;
} ValueGroupInfo;

typedef struct {
    struct _GwyResultsPrivate *parent;
    const gchar *label;
    gchar *unit_str;
    gchar *format;
    gchar *symbol;
    GString *value_formatted;
    GString *error_formatted;
    GString *units_formatted;
    GwyResultsValueType type;
    gboolean is_header : 1;
    gboolean is_separator : 1;
    gboolean is_format : 1;
    gboolean is_angle : 1;
    gboolean is_percents : 1;
    gboolean is_covar_matrix : 1;
    gboolean is_fitting_param : 1;
    gboolean translate_format : 1;
    gboolean translate_unit : 1;
    gboolean translate_strval : 1;
    gboolean has_value : 1;  /* Negation of is_na, to default to zero. */
    gboolean has_error : 1;
    gshort power_t;
    gshort power_u;
    gshort power_v;
    gshort power_w;
    gshort power_x;
    gshort power_y;
    gshort power_z;
    gshort precision;
    guint bind_group;
    guint label_width;   /* Just a cache; varies according to translation. */
    guint covar_n;
    ResultValueAny value;
    ResultValueAny error;
    ValueSet value_set;
    gdouble *covar_matrix;
    gboolean *fixed_params;
} ResultLine;

struct _GwyResultsPrivate {
    guint n;
    guint size;
    GQuark *ids;   /* Keep separated from lines for better access pattern. */
    ResultLine *lines;
    GwySIValueFormat *vf;
    GString *str;
    GwySIUnit *tunit;
    GwySIUnit *uunit;
    GwySIUnit *vunit;
    GwySIUnit *wunit;
    GwySIUnit *xunit;
    GwySIUnit *yunit;
    GwySIUnit *zunit;
    GwySIUnit *unit;
    gboolean formatted_are_valid;
    guint ngroups;
    ValueGroupInfo *groups;
};

typedef struct _GwyResultsPrivate ResultsPriv;

static void gwy_results_finalize(GObject *object);

G_DEFINE_TYPE(GwyResults, gwy_results, G_TYPE_OBJECT);

static void
results_maybe_resize(ResultsPriv *results)
{
    guint newsize;

    if (results->n < results->size)
        return;

    newsize = MAX(8, 2*results->size);
    results->lines = g_renew(ResultLine, results->lines, newsize);
    results->ids = g_renew(GQuark, results->ids, newsize);
    gwy_clear(results->lines + results->size, newsize - results->size);
    results->size = newsize;
}

static void
value_set_maybe_resize(ValueSet *vset)
{
    if (vset->n < vset->size)
        return;

    vset->size = MAX(8, 2*vset->size);
    vset->values = g_renew(ResultValue, vset->values, vset->size);
}

static inline gchar
result_value_get_format_char(const ResultValue *value, const gchar *format)
{
    return format[value->fbegin + value->flen-1];
}

static void
value_set_clear(ValueSet *vset, const gchar *format)
{
    guint i;

    for (i = 0; i < vset->n; i++) {
        ResultValue *value = vset->values + i;
        if (value->is_set && result_value_get_format_char(value, format) == 's')
            g_free(value->value.s);
    }
    gwy_clear(vset->values, vset->n);
    vset->n = 0;
}

static inline gint
result_value_name_equal(const ResultValue *value, const gchar *format,
                        const gchar *s)
{
    guint slen = strlen(s), vlen = value->flen-4;
    return slen == vlen && strncmp(format + value->fbegin+2, s, vlen) == 0;
}

static ResultValue*
find_result_value(ValueSet *vset, const gchar *format, const gchar *name)
{
    guint i;

    for (i = 0; i < vset->n; i++) {
        if (result_value_name_equal(vset->values + i, format, name))
            return vset->values + i;
    }
    return NULL;
}

static void
gwy_results_class_init(GwyResultsClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(ResultsPriv));
    gobject_class->finalize = gwy_results_finalize;
}

static void
gwy_results_init(GwyResults *results)
{
    ResultsPriv *priv;

    results->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE(results, GWY_TYPE_RESULTS, ResultsPriv);
    priv->str = g_string_new(NULL);
    priv->unit = gwy_si_unit_new(NULL);
    priv->vf = gwy_si_unit_value_format_new(1.0, 4, "");
}

static void
gwy_results_finalize(GObject *object)
{
    ResultsPriv *results = ((GwyResults*)object)->priv;
    guint i;

    for (i = 0; i < results->n; i++) {
        ResultLine *line = results->lines + i;

        if (line->is_format) {
            value_set_clear(&line->value_set, line->format);
            g_assert(!line->has_error);
        }
        else if (line->is_covar_matrix) {
            g_free(line->fixed_params);
            g_free(line->covar_matrix);
        }
        else if (line->type == GWY_RESULTS_VALUE_STRING) {
            g_free(line->value.s);
            if (line->has_error)
                g_free(line->error.s);
        }

        g_free(line->unit_str);
        g_free(line->symbol);
        g_free(line->format);

        if (line->value_formatted)
            g_string_free(line->value_formatted, TRUE);
        if (line->error_formatted)
            g_string_free(line->error_formatted, TRUE);
        if (line->units_formatted)
            g_string_free(line->units_formatted, TRUE);
    }
    g_free(results->lines);
    g_free(results->ids);

    for (i = 1; i <= results->ngroups; i++) {
        if (results->groups[i].vf)
            gwy_si_unit_value_format_free(results->groups[i].vf);
    }
    g_free(results->groups);

    GWY_OBJECT_UNREF(results->tunit);
    GWY_OBJECT_UNREF(results->uunit);
    GWY_OBJECT_UNREF(results->vunit);
    GWY_OBJECT_UNREF(results->wunit);
    GWY_OBJECT_UNREF(results->xunit);
    GWY_OBJECT_UNREF(results->yunit);
    GWY_OBJECT_UNREF(results->zunit);
    GWY_OBJECT_UNREF(results->unit);
    gwy_si_unit_value_format_free(results->vf);
    g_string_free(results->str, TRUE);
}

/**
 * gwy_results_new:
 *
 * Creates a new empty set of reported scalar values.
 *
 * Returns: A new set of reported scalar values.
 *
 * Since: 2.50
 **/
GwyResults*
gwy_results_new(void)
{
    return g_object_new(GWY_TYPE_RESULTS, NULL);
}

/**
 * gwy_results_copy:
 * @results: A set of reported scalar values.
 *
 * Creates a copy of a set of reported scalar values.
 *
 * Returns: A new #GwyResults which is an exact copy of @results.
 *
 * Since: 2.50
 **/
GwyResults*
gwy_results_copy(GwyResults *results)
{
    GwyResults *copy;
    ResultsPriv *priv;
    guint i, j;

    g_return_val_if_fail(GWY_IS_RESULTS(results), NULL);

    copy = gwy_results_new();
    priv = copy->priv;
    *priv = *results->priv;

    priv->ids = g_memdup(priv->ids, priv->size*sizeof(GQuark));
    priv->lines = g_memdup(priv->lines, priv->size*sizeof(ResultLine));
    for (i = 0; i < priv->n; i++) {
        ResultLine *line = priv->lines + i;

        line->unit_str = g_strdup(line->unit_str);
        line->symbol = g_strdup(line->symbol);
        line->format = g_strdup(line->format);
        if (line->value_formatted)
            line->value_formatted = g_string_new_len(line->value_formatted->str, line->value_formatted->len);
        if (line->error_formatted)
            line->error_formatted = g_string_new_len(line->error_formatted->str, line->error_formatted->len);
        if (line->units_formatted)
            line->units_formatted = g_string_new_len(line->units_formatted->str, line->units_formatted->len);

        if (line->is_format) {
            ValueSet *vset = &line->value_set;
            vset->values = g_memdup(vset->values, vset->size*sizeof(ResultValue));
            for (j = 0; j < vset->n; j++) {
                if (result_value_get_format_char(vset->values + j, line->format) == 's')
                    vset->values[i].value.s = g_strdup(vset->values[i].value.s);
            }
        }
        else if (line->is_covar_matrix) {
            j = line->covar_n;
            if (line->covar_matrix)
                line->covar_matrix = g_memdup(line->covar_matrix, j*(j+1)/2*sizeof(gdouble));
            if (line->fixed_params)
                line->fixed_params = g_memdup(line->fixed_params, j*sizeof(gdouble));
        }
        else if (line->type == GWY_RESULTS_VALUE_STRING) {
            line->value.s = g_strdup(line->value.s);
            if (line->has_error)
                line->error.s = g_strdup(line->error.s);
        }
    }

    priv->groups = g_memdup(priv->groups, (priv->ngroups + 1)*sizeof(ValueGroupInfo));
    for (i = 1; i <= priv->ngroups; i++) {
        if (priv->groups[i].vf)
            priv->groups[i].vf = gwy_si_unit_value_format_copy(priv->groups[i].vf);
    }

    if (priv->tunit)
        priv->tunit = gwy_si_unit_duplicate(priv->tunit);
    if (priv->uunit)
        priv->uunit = gwy_si_unit_duplicate(priv->uunit);
    if (priv->vunit)
        priv->vunit = gwy_si_unit_duplicate(priv->vunit);
    if (priv->wunit)
        priv->wunit = gwy_si_unit_duplicate(priv->wunit);
    if (priv->xunit)
        priv->xunit = gwy_si_unit_duplicate(priv->xunit);
    if (priv->yunit)
        priv->yunit = gwy_si_unit_duplicate(priv->yunit);
    if (priv->zunit)
        priv->zunit = gwy_si_unit_duplicate(priv->zunit);
    if (priv->unit)
        priv->unit = gwy_si_unit_duplicate(priv->unit);

    priv->vf = gwy_si_unit_value_format_copy(priv->vf);
    priv->str = g_string_new(priv->str->str);

    return copy;
}

/**
 * gwy_results_add_header:
 * @results: A set of reported scalar values.
 * @label: Header text (untranslated).
 *
 * Appends a header line to the set of reported scalar values.
 *
 * Since: 2.50
 **/
void
gwy_results_add_header(GwyResults *results,
                       const gchar *label)
{
    ResultLine *line;
    ResultsPriv *priv;

    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(label);

    priv = results->priv;
    results_maybe_resize(priv);
    priv->ids[priv->n] = 0;
    line = priv->lines + priv->n;
    line->parent = priv;
    line->is_header = TRUE;
    line->label = label;
    priv->n++;
}

/**
 * gwy_results_add_separator:
 * @results: A set of reported scalar values.
 *
 * Appends a separator line to the set of reported scalar values.
 *
 * Since: 2.50
 **/
void
gwy_results_add_separator(GwyResults *results)
{
    ResultLine *line;
    ResultsPriv *priv;

    g_return_if_fail(GWY_IS_RESULTS(results));

    priv = results->priv;
    results_maybe_resize(priv);
    priv->ids[priv->n] = 0;
    line = priv->lines + priv->n;
    line->parent = priv;
    line->is_separator = TRUE;
    priv->n++;
}

/* XXX: We can add a hash table.  However, comparing integers is rather cheap.
 * So do not bother unless really large result sets start to appear. */
static ResultLine*
find_result_line(const ResultsPriv *results, GQuark quark)
{
    const GQuark *ids = results->ids;
    guint i;

    for (i = 0; i < results->n; i++) {
        if (quark == ids[i])
            return results->lines + i;
    }

    return NULL;
}

static gboolean
check_unique_id(const ResultsPriv *results, GQuark quark)
{
    if (!find_result_line(results, quark))
        return TRUE;

    g_warning("Ignoring duplicate result with id %s.", g_quark_to_string(quark));
    return FALSE;
}

static const gchar*
find_line_str_id(ResultLine *line)
{
    ResultsPriv *results = line->parent;
    guint i;

    g_return_val_if_fail(GWY_IS_RESULTS(results), "???");
    i = results->lines - line;
    g_return_val_if_fail(i < results->n, "???");

    return g_quark_to_string(results->ids[i]);
}

static void
handle_value_properties(ResultLine *line, va_list ap)
{
    const gchar *propname;

    while ((propname = va_arg(ap, gchar*))) {
        if (gwy_strequal(propname, "type")) {
            if (line->is_format) {
                g_warning("Cannot set type for formatted result %s.", find_line_str_id(line));
            }
            else {
                line->type = va_arg(ap, gint);  /* promotion */
                if (line->type < GWY_RESULTS_VALUE_FLOAT || line->type > GWY_RESULTS_VALUE_YESNO) {
                    g_warning("Invalid type %u for result %s, assuming double.", line->type, find_line_str_id(line));
                    line->type = GWY_RESULTS_VALUE_FLOAT;
                }
            }
        }
        else if (gwy_strequal(propname, "power-t"))
            line->power_t = va_arg(ap, gint);
        else if (gwy_strequal(propname, "power-u"))
            line->power_u = va_arg(ap, gint);
        else if (gwy_strequal(propname, "power-v"))
            line->power_v = va_arg(ap, gint);
        else if (gwy_strequal(propname, "power-w"))
            line->power_w = va_arg(ap, gint);
        else if (gwy_strequal(propname, "power-x"))
            line->power_x = va_arg(ap, gint);
        else if (gwy_strequal(propname, "power-y"))
            line->power_y = va_arg(ap, gint);
        else if (gwy_strequal(propname, "power-z"))
            line->power_z = va_arg(ap, gint);
        else if (gwy_strequal(propname, "precision"))
            line->precision = va_arg(ap, gint);
        else if (gwy_strequal(propname, "is-angle"))
            line->is_angle = !!va_arg(ap, gint);  /* promotion */
        else if (gwy_strequal(propname, "is-fitting-param"))
            line->is_fitting_param = !!va_arg(ap, gint);  /* promotion */
        else if (gwy_strequal(propname, "is-percents"))
            line->is_percents = !!va_arg(ap, gint);  /* promotion */
        else if (gwy_strequal(propname, "translate-unit"))
            line->translate_unit = !!va_arg(ap, gint);  /* promotion */
        else if (gwy_strequal(propname, "translate-string"))
            line->translate_strval = !!va_arg(ap, gint);  /* promotion */
        else if (gwy_strequal(propname, "unit-str"))
            gwy_assign_string(&line->unit_str, va_arg(ap, gchar*));
        else if (gwy_strequal(propname, "symbol"))
            gwy_assign_string(&line->symbol, va_arg(ap, gchar*));
        else {
            g_warning("Ignoring unknown property %s of value %s.",
                      propname, find_line_str_id(line));
        }
    }
}

static ResultLine*
add_result_line_base(GwyResults *results,
                     const gchar *id,
                     const gchar *label,
                     GwyResultsValueType type)
{
    ResultLine *line;
    ResultsPriv *priv;
    GQuark quark;

    g_return_val_if_fail(GWY_IS_RESULTS(results), NULL);
    g_return_val_if_fail(id, NULL);

    priv = results->priv;
    if (!label)
        label = "";

    quark = g_quark_from_string(id);
    if (!check_unique_id(priv, quark))
        return NULL;

    results_maybe_resize(priv);
    priv->ids[priv->n] = quark;
    line = priv->lines + priv->n;
    line->parent = priv;
    line->label = label;
    line->type = type;
    line->precision = -1;
    priv->n++;

    return line;
}

/**
 * gwy_results_add_value:
 * @results: A set of reported scalar values.
 * @id: Value identifier (unique in @results).
 * @label: Value label text (untranslated).
 * @...: %NULL-terminated sequence of key, value pairs specifying value properties.
 *
 * Appends a general value line to a set of reported scalar values.
 *
 * Possible keys include:
 * <simplelist type='vert'>
 * <member><literal>"type"</literal>, value type (#GwyResultsValueType)</member>
 * <member><literal>"power-t"</literal>, power of unit @t (#gint)</member>
 * <member><literal>"power-u"</literal>, power of unit @u (#gint, since 2.52)</member>
 * <member><literal>"power-v"</literal>, power of unit @v (#gint, since 2.52)</member>
 * <member><literal>"power-x"</literal>, power of unit @x (#gint)</member>
 * <member><literal>"power-y"</literal>, power of unit @y (#gint)</member>
 * <member><literal>"power-z"</literal>, power of unit @z (#gint)</member>
 * <member><literal>"power-w"</literal>, power of unit @w (#gint)</member>
 * <member><literal>"precision"</literal>, the mininum number of significant digits (#gint, since 2.63)</member>
 * <member><literal>"is-angle"</literal>, %TRUE if the quantity should converted to degrees for humans, this overrides any unit powers (#gboolean)</member>
 * <member><literal>"is-fitting-param"</literal>, %TRUE if the quantity should be reported as fixed parameter when it does not have error set (#gboolean)</member>
 * <member><literal>"is-percents"</literal>, %TRUE if the quantity should converted to percents for humans, this overrides any unit powers (#gboolean)</member>
 * <member><literal>"unit-str"</literal>, fixed unit, this overrides any unit powers (#gchar*)</member>
 * <member><literal>"translate-unit"</literal>, %TRUE if the fixed unit should be translated for humans (#gboolean)</member>
 * <member><literal>"translate-string"</literal>, %TRUE if string values should be translated for humans (#gboolean)</member>
 * <member><literal>"symbol"</literal>, optional untranslatable symbol for the quantity (#gchar*)</member>
 * </simplelist>
 *
 * The type of value passed to gwy_results_fill_values() must match the type given here.  The default properties are
 * floating point value, zero unit powers, no fixed unit string, and all boolean properties %FALSE.
 *
 * In most cases this function is not necessary and you can use one of the convenience functions such as
 * gwy_results_add_value_z() or gwy_results_add_value_angle().
 *
 * Since: 2.50
 **/
void
gwy_results_add_value(GwyResults *results,
                      const gchar *id,
                      const gchar *label,
                      ...)
{
    ResultLine *line;
    va_list ap;

    line = add_result_line_base(results, id, label, GWY_RESULTS_VALUE_FLOAT);
    g_return_if_fail(line);

    va_start(ap, label);
    handle_value_properties(line, ap);
    va_end(ap);
}

static void
parse_format(const gchar *format, ValueSet *vset)
{
    ResultValue *value;
    const gchar *s = format, *t;
    guint fbegin, fend;

    while ((s = strchr(s, '%'))) {
        if (s[1] == '%') {
            s += 2;
            continue;
        }
        if (s[1] != '{') {
            g_warning("Percent not followed by %% or { in format %s.", format);
            s++;
            continue;
        }

        fbegin = s-format;
        s += 2;
        if (!(t = strchr(s, '}'))) {
            g_warning("Missing closing } in format %s.", format);
            continue;
        }
        if (t[1] != 'i' && t[1] != 'v' && t[1] != 's' && t[1] != 'y') {
            g_warning("Unknown format character %c in format %s.", t[1], format);
            s = t;
            continue;
        }
        t += 2;
        fend = t-format;
        value_set_maybe_resize(vset);
        value = vset->values + vset->n;
        value->fbegin = fbegin;
        value->flen = fend-fbegin;
        value->is_set = FALSE;
        gwy_clear(&value->value, 1);
        vset->n++;
        s = t;
    }
}

static guint
value_set_find(const ValueSet *vset, const gchar *format,
               const ResultValue *v, const gchar *vformat)
{
    const ResultValue *value;
    guint i;

    for (i = 0; i < vset->n; i++) {
        value = vset->values + i;
        if (v->flen == value->flen && strncmp(vformat + v->fbegin, format + value->fbegin, v->flen) == 0)
            return i;
    }

    return G_MAXUINT;
}

/* Check if @vseta is subset of @vsetb.  Equality is reached when it holds also the other way round. */
static gboolean
value_set_is_subset(const ValueSet *vseta, const ValueSet *vsetb,
                    const gchar *formata, const gchar *formatb)
{
    guint i;

    for (i = 0; i < vseta->n; i++) {
        if (value_set_find(vsetb, formatb, vseta->values + i, formata) == G_MAXUINT)
            return FALSE;
    }
    return TRUE;
}

static void
value_set_copy_values(const ValueSet *src, ValueSet *dest,
                      const gchar *srcformat, const gchar *destformat)
{
    const ResultValue *vsrc;
    ResultValue *vdest;
    guint i, j;

    for (i = 0; i < dest->n; i++) {
        vdest = dest->values + i;
        j = value_set_find(src, srcformat, vdest, destformat);
        if (j == G_MAXUINT) {
            g_critical("Mismatched field names.");
            continue;
        }
        vsrc = src->values + j;
        vdest->value = vsrc->value;
        vdest->is_set = vsrc->is_set;
        if (result_value_get_format_char(vsrc, srcformat) == 's')
            gwy_assign_string(&vdest->value.s, vsrc->value.s);
        else
            vdest->value = vsrc->value;
    }
}

/**
 * gwy_results_add_format:
 * @results: A set of reported scalar values.
 * @id: Value identifier (unique in @results).
 * @label: Value label text (untranslated).
 * @translate_format: %TRUE to translate the format string when formatting for humans.
 * @format: Format string for the value.
 * @...: %NULL-terminated sequence of key, value pairs specifying value properties.  See gwy_results_add_value().
 *
 * Appends a formatted value line to a set of reported scalar values.
 *
 * This function creates a line that can contain multiple individual fields and must be filled using
 * gwy_results_fill_format().
 *
 * The format string contains replaceable fields of the form "%{name}c" where @name is the field name, used later in
 * gwy_results_fill_format() and @c is the format character which can be one of
 * <simplelist type='vert'>
 * <member><literal>"y"</literal>, yes/no value (#gboolean)</member>
 * <member><literal>"i"</literal>, integer value (#gint)</member>
 * <member><literal>"s"</literal>, string value (#gchar*)</member>
 * <member><literal>"v"</literal>, floating point value with units (#gdouble)</member>
 * </simplelist>
 *
 * All indivdual "v" fields share one value format and units.
 *
 * Since: 2.50
 **/
void
gwy_results_add_format(GwyResults *results,
                       const gchar *id,
                       const gchar *label,
                       gboolean translate_format,
                       const gchar *format,
                       ...)
{
    ResultLine *line;
    va_list ap;

    line = add_result_line_base(results, id, label, 0);
    g_return_if_fail(line);

    line->is_format = TRUE;
    line->format = g_strdup(format);
    line->translate_format = translate_format;
    parse_format(format, &line->value_set);
    if (translate_format) {
        ValueSet tvset;
        const gchar *tformat = _(format);

        gwy_clear(&tvset, 1);
        parse_format(tformat, &tvset);
        if (!value_set_is_subset(&line->value_set, &tvset, format, tformat)
            || !value_set_is_subset(&tvset, &line->value_set, tformat, format)) {
            g_warning("Format %s translates to %s which has different fields.", format, tformat);
        }
        value_set_clear(&tvset, tformat);
    }
    va_start(ap, format);
    handle_value_properties(line, ap);
    va_end(ap);
}

/**
 * gwy_results_add_value_str:
 * @results: A set of reported scalar values.
 * @id: Value identifier (unique in @results).
 * @label: Value label text (untranslated).
 *
 * Appends a string value line to a set of reported scalar values.
 *
 * The corresponding value should be passed as #gchar*.
 *
 * Since: 2.50
 **/
void
gwy_results_add_value_str(GwyResults *results,
                          const gchar *id,
                          const gchar *label)
{
    add_result_line_base(results, id, label, GWY_RESULTS_VALUE_STRING);
}

/**
 * gwy_results_add_value_x:
 * @results: A set of reported scalar values.
 * @id: Value identifier (unique in @results).
 * @label: Value label text (untranslated).
 *
 * Appends an @x-like floating point value line to a set of reported scalar values.
 *
 * The corresponding value should be passed as #gdouble.
 *
 * Since: 2.50
 **/
void
gwy_results_add_value_x(GwyResults *results,
                        const gchar *id,
                        const gchar *label)
{
    ResultLine *line;

    line = add_result_line_base(results, id, label, GWY_RESULTS_VALUE_FLOAT);
    g_return_if_fail(line);
    line->power_x = 1;
}

/**
 * gwy_results_add_value_y:
 * @results: A set of reported scalar values.
 * @id: Value identifier (unique in @results).
 * @label: Value label text (untranslated).
 *
 * Appends an @y-like floating point value line to a set of reported scalar values.
 *
 * The corresponding value should be passed as #gdouble.
 *
 * Since: 2.61
 **/
void
gwy_results_add_value_y(GwyResults *results,
                        const gchar *id,
                        const gchar *label)
{
    ResultLine *line;

    line = add_result_line_base(results, id, label, GWY_RESULTS_VALUE_FLOAT);
    g_return_if_fail(line);
    line->power_y = 1;
}

/**
 * gwy_results_add_value_z:
 * @results: A set of reported scalar values.
 * @id: Value identifier (unique in @results).
 * @label: Value label text (untranslated).
 *
 * Appends a @z-like floating point value line to a set of reported scalar values.
 *
 * The corresponding value should be passed as #gdouble.
 *
 * Since: 2.50
 **/
void
gwy_results_add_value_z(GwyResults *results,
                        const gchar *id,
                        const gchar *label)
{
    ResultLine *line;

    line = add_result_line_base(results, id, label, GWY_RESULTS_VALUE_FLOAT);
    g_return_if_fail(line);
    line->power_z = 1;
}

/**
 * gwy_results_add_value_plain:
 * @results: A set of reported scalar values.
 * @id: Value identifier (unique in @results).
 * @label: Value label text (untranslated).
 *
 * Appends a unitless floating point value line to a set of reported scalar values.
 *
 * The corresponding value should be passed as #gdouble.
 *
 * Since: 2.50
 **/
void
gwy_results_add_value_plain(GwyResults *results,
                            const gchar *id,
                            const gchar *label)
{
    ResultLine *line;

    line = add_result_line_base(results, id, label, GWY_RESULTS_VALUE_FLOAT);
    g_return_if_fail(line);
}

/**
 * gwy_results_add_value_int:
 * @results: A set of reported scalar values.
 * @id: Value identifier (unique in @results).
 * @label: Value label text (untranslated).
 *
 * Appends an integer value line to a set of reported scalar values.
 *
 * The corresponding value should be passed as #gint.
 *
 * Since: 2.50
 **/
void
gwy_results_add_value_int(GwyResults *results,
                          const gchar *id,
                          const gchar *label)
{
    add_result_line_base(results, id, label, GWY_RESULTS_VALUE_INT);
}

/**
 * gwy_results_add_value_angle:
 * @results: A set of reported scalar values.
 * @id: Value identifier (unique in @results).
 * @label: Value label text (untranslated).
 *
 * Appends an angle-like floating point value line to a set of reported scalar values.
 *
 * The corresponding value should be passed as #gdouble.
 *
 * Since: 2.50
 **/
void
gwy_results_add_value_angle(GwyResults *results,
                            const gchar *id,
                            const gchar *label)
{
    ResultLine *line;

    line = add_result_line_base(results, id, label, GWY_RESULTS_VALUE_FLOAT);
    g_return_if_fail(line);
    line->is_angle = TRUE;
}

/**
 * gwy_results_add_value_percents:
 * @results: A set of reported scalar values.
 * @id: Value identifier (unique in @results).
 * @label: Value label text (untranslated).
 *
 * Appends a percent-like floating point value line to a set of reported scalar values.
 *
 * The corresponding value should be passed as #gdouble.
 *
 * Since: 2.50
 **/
void
gwy_results_add_value_percents(GwyResults *results,
                               const gchar *id,
                               const gchar *label)
{
    ResultLine *line;

    line = add_result_line_base(results, id, label, GWY_RESULTS_VALUE_FLOAT);
    g_return_if_fail(line);
    line->is_percents = TRUE;
}

/**
 * gwy_results_add_value_yesno:
 * @results: A set of reported scalar values.
 * @id: Value identifier (unique in @results).
 * @label: Value label text (untranslated).
 *
 * Appends a yes/no value line to a set of reported scalar values.
 *
 * The corresponding value should be passed as #gboolean (which is the same as #gint).
 *
 * Since: 2.50
 **/
void
gwy_results_add_value_yesno(GwyResults *results,
                            const gchar *id,
                            const gchar *label)
{
    add_result_line_base(results, id, label, GWY_RESULTS_VALUE_YESNO);
}

static void
setup_covariance_matrix_data(ResultLine *line, guint n, const gchar *symbols)
{
    line->is_covar_matrix = TRUE;
    line->covar_n = n;
    line->symbol = g_strdup(symbols);
    line->covar_matrix = g_new(gdouble, n*(n+1)/2);
    line->fixed_params = g_new0(gboolean, n);
}

/**
 * gwy_results_add_covariance_matrix:
 * @results: A set of reported scalar values.
 * @id: Value identifier (unique in @results).
 * @label: Value label text (untranslated).
 * @...: Sequence of @n parameter symbols (as #gchar*), where @n is the matrix dimension.
 *
 * Appends a covariance matrix to a set of reported scalar values.
 *
 * Covariance matrices are multi-line.  They cannot be formatted using the standard functions such as
 * gwy_results_get_value().  They are also omitted when formatting as CSV (%GWY_RESULTS_REPORT_CSV) because CSV
 * requires the same number of values on each line.
 *
 * Since: 2.50
 **/
void
gwy_results_add_covariance_matrix(GwyResults *results,
                                  const gchar *id,
                                  const gchar *label,
                                  ...)
{
    ResultLine *line;
    const gchar *symbol;
    GString *str;
    va_list ap;
    guint n = 0;

    g_return_if_fail(GWY_IS_RESULTS(results));

    line = add_result_line_base(results, id, label, 0);
    g_return_if_fail(line);

    va_start(ap, label);
    str = results->priv->str;
    g_string_truncate(str, 0);
    while ((symbol = va_arg(ap, const gchar*))) {
        g_string_append(str, symbol);
        g_string_append_c(str, '\n');
        n++;
    }
    va_end(ap);

    g_return_if_fail(n);
    setup_covariance_matrix_data(line, n, str->str);
}

/**
 * gwy_results_add_covariance_matrixv:
 * @results: A set of reported scalar values.
 * @id: Value identifier (unique in @results).
 * @label: Value label text (untranslated).
 * @n: Matrix dimension.
 * @symbols: Array of @n parameter symbols (as #gchar*).
 *
 * Appends a covariance matrix to a set of reported scalar values.
 *
 * See gwy_results_add_covariance_matrix() for discussion.
 *
 * Since: 2.50
 **/
void
gwy_results_add_covariance_matrixv(GwyResults *results,
                                   const gchar *id,
                                   const gchar *label,
                                   guint n,
                                   const gchar **symbols)
{
    ResultLine *line;
    GString *str;
    guint i;

    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(n);

    line = add_result_line_base(results, id, label, 0);
    g_return_if_fail(line);
    str = results->priv->str;
    g_string_truncate(str, 0);
    for (i = 0; i < n; i++) {
        g_return_if_fail(symbols[i]);
        g_string_append(str, symbols[i]);
        g_string_append_c(str, '\n');
    }
    setup_covariance_matrix_data(line, n, str->str);
}

static void
clear_single_member_groups(ResultsPriv *results)
{
    guint i;

    for (i = 0; i < results->n; i++) {
        ResultLine *line = results->lines + i;
        if (line->bind_group && results->groups[line->bind_group].nmembers == 1) {
            results->groups[line->bind_group].nmembers = 0;
            line->bind_group = 0;
        }
    }
}

/**
 * gwy_results_bind_formats:
 * @results: A set of reported scalar values.
 * @id: First value identifier to bind.
 * @...: Identifiers of other values to bind, %NULL-terminated.
 *
 * Binds formats of several floating point values together.
 *
 * This is useful when you do not want one of related values displayed in nm and another in µm (for instance), just
 * because it is somewhat larger.
 *
 * The values must already exist in @results.  Only values of the same type can be bound together -- and binding has
 * any useful purpose for floating point values anyway.  The values must also have the same unit powers.
 *
 * Rebinding a value that is already bound to another group is not allowed. However it is possible to add more values
 * to the group of the first value (@id).  Single-value groups are not created.
 *
 * Since: 2.50
 **/
void
gwy_results_bind_formats(GwyResults *results,
                         const gchar *id,
                         ...)
{
    ResultLine *line;
    ResultsPriv *priv;
    GQuark quark;
    va_list ap;
    guint bindgroup;
    guint i;

    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(id);

    priv = results->priv;

    /* Find and recycle a currently empty group or create a new one. */
    bindgroup = 0;
    for (i = 1; i <= priv->ngroups; i++) {
        if (!priv->groups[i].nmembers) {
            bindgroup = i;
            break;
        }
    }
    if (!bindgroup) {
        priv->ngroups++;
        bindgroup = priv->ngroups;

        /* This is OK even when we do not actually use the group because then we find it next time as free and thus
         * not allocate a new one. */
        priv->groups = g_renew(ValueGroupInfo, priv->groups, priv->ngroups+1);
        gwy_clear(priv->groups + 0, 1);
        gwy_clear(priv->groups + priv->ngroups, 1);
    }

    i = 0;
    va_start(ap, id);
    do {
        i++;
        quark = g_quark_from_string(id);
        if (!(line = find_result_line(priv, quark))) {
            g_warning("Cannot find result %s.", id);
            continue;
        }
        if (line->type != GWY_RESULTS_VALUE_FLOAT) {
            g_warning("Cannot bind format of non-float result %s.", id);
            continue;
        }
        if (line->bind_group) {
            if (i == 1)
                bindgroup = line->bind_group;
            else {
                g_warning("Result %s is already bound to group #%u.", id, line->bind_group);
            }
            continue;
        }
        /* Possible other checks: same unit powers; same angle flags.
         * Too much work... */
        line->bind_group = bindgroup;
        priv->groups[bindgroup].nmembers++;
    } while ((id = va_arg(ap, const gchar*)));
    va_end(ap);

    clear_single_member_groups(priv);

    priv->formatted_are_valid = FALSE;
}

/**
 * gwy_results_unbind_formats:
 * @results: A set of reported scalar values.
 * @id: First value identifier to unbind.
 * @...: Identifiers of other values to unbind, %NULL-terminated.
 *
 * Unbinds formats of floating point values.
 *
 * The values are removed from their respective groups.  It is not necessary for them to belong to the same group, or
 * to any group.
 *
 * Since: 2.50
 **/
void
gwy_results_unbind_formats(GwyResults *results,
                           const gchar *id,
                           ...)
{
    ResultLine *line;
    ResultsPriv *priv;
    GQuark quark;
    va_list ap;

    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(id);

    priv = results->priv;
    va_start(ap, id);
    do {
        quark = g_quark_from_string(id);
        if (!(line = find_result_line(priv, quark))) {
            g_warning("Cannot find result %s.", id);
            continue;
        }
        if (!line->bind_group)
            continue;

        g_assert(priv->groups[line->bind_group].nmembers > 0);
        priv->groups[line->bind_group].nmembers--;
        line->bind_group = 0;
    } while ((id = va_arg(ap, const gchar*)));
    va_end(ap);

    clear_single_member_groups(priv);
    priv->formatted_are_valid = FALSE;
}

static void
transfer_unit(GwySIUnit *src, GwySIUnit **dest)
{
    if (src && *dest)
        gwy_serializable_clone(G_OBJECT(src), G_OBJECT(*dest));
    else if (src && !*dest)
        *dest = gwy_si_unit_duplicate(src);
    else if ((!src || gwy_si_unit_equal_string(src, NULL)) && *dest)
        GWY_OBJECT_UNREF(*dest);
    /* else both are empty */
}

/**
 * gwy_results_set_unit:
 * @results: A set of reported scalar values.
 * @name: Unit name, one of "t", "w", "x", "y" or "z".
 * @unit: The SI unit to use (@results makes a copy; it does not take a reference).  It can be %NULL.
 *
 * Sets the unit corresponding to one axis in a set of reported scalar values, from an SI unit.
 *
 * Unit names "u" and "v" are available since 2.52.
 *
 * Since: 2.50
 **/
void
gwy_results_set_unit(GwyResults *results,
                     const gchar *name,
                     GwySIUnit *unit)
{
    ResultsPriv *priv;
    gchar u;

    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(name);
    g_return_if_fail(strlen(name) == 1);

    priv = results->priv;
    u = name[0];
    if (u == 't')
        transfer_unit(unit, &priv->tunit);
    else if (u == 'u')
        transfer_unit(unit, &priv->uunit);
    else if (u == 'v')
        transfer_unit(unit, &priv->vunit);
    else if (u == 'w')
        transfer_unit(unit, &priv->wunit);
    else if (u == 'x')
        transfer_unit(unit, &priv->xunit);
    else if (u == 'y')
        transfer_unit(unit, &priv->yunit);
    else if (u == 'z')
        transfer_unit(unit, &priv->zunit);
    else {
        g_warning("Ignoring unknown unit %s.", name);
    }

    priv->formatted_are_valid = FALSE;
}

static void
transfer_unit_str(const gchar *unitstr, GwySIUnit **dest)
{
    if (unitstr && *dest)
        gwy_si_unit_set_from_string(*dest, unitstr);
    else if (unitstr && !*dest)
        *dest = gwy_si_unit_new(unitstr);
    else if ((!unitstr || !strlen(unitstr)) && *dest)
        GWY_OBJECT_UNREF(*dest);
    /* else both are empty */
}

/**
 * gwy_results_set_unit_str:
 * @results: A set of reported scalar values.
 * @name: Unit name, one of "t", "u", "v", "w", "x", "y" or "z".
 * @unitstr: The unit to use, as a string.  It can be %NULL.
 *
 * Sets the unit corresponding to one axis in a set of reported scalar values, from a string.
 *
 * Unit names "u" and "v" are available since 2.52.
 *
 * Since: 2.50
 **/
void
gwy_results_set_unit_str(GwyResults *results,
                         const gchar *name,
                         const gchar *unitstr)
{
    ResultsPriv *priv;
    gchar u;

    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(name);
    g_return_if_fail(strlen(name) == 1);

    priv = results->priv;
    u = name[0];
    if (u == 't')
        transfer_unit_str(unitstr, &priv->tunit);
    else if (u == 'u')
        transfer_unit_str(unitstr, &priv->uunit);
    else if (u == 'v')
        transfer_unit_str(unitstr, &priv->vunit);
    else if (u == 'w')
        transfer_unit_str(unitstr, &priv->wunit);
    else if (u == 'x')
        transfer_unit_str(unitstr, &priv->xunit);
    else if (u == 'y')
        transfer_unit_str(unitstr, &priv->yunit);
    else if (u == 'z')
        transfer_unit_str(unitstr, &priv->zunit);
    else {
        g_warning("Ignoring unknown unit %s.", name);
    }

    priv->formatted_are_valid = FALSE;
}

static void
fill_values(ResultsPriv *results, gboolean with_errors, va_list ap)
{
    ResultLine *line;
    const gchar *id;
    GQuark quark;

    while ((id = va_arg(ap, const gchar*))) {
        quark = g_quark_from_string(id);
        if (!(line = find_result_line(results, quark))) {
            g_warning("Cannot find result %s.  Aborting value filling to avoid crash.", id);
            break;
        }
        if (line->type == GWY_RESULTS_VALUE_FLOAT) {
            line->value.d = va_arg(ap, gdouble);
            if (with_errors)
                line->error.d = va_arg(ap, gdouble);
        }
        else if (line->type == GWY_RESULTS_VALUE_STRING) {
            gwy_assign_string(&line->value.s, va_arg(ap, gchar*));
            if (with_errors)
                gwy_assign_string(&line->error.s, va_arg(ap, gchar*));
        }
        else if (line->type == GWY_RESULTS_VALUE_INT) {
            line->value.i = va_arg(ap, gint);
            if (with_errors)
                line->error.i = va_arg(ap, gint);
        }
        else if (line->type == GWY_RESULTS_VALUE_YESNO) {
            line->value.yn = va_arg(ap, gint);
            if (with_errors) {
                g_warning("Yes/no result %s should not have an error.  This is silly.", id);
                line->error.yn = va_arg(ap, gint);
            }
        }
        else {
            g_warning("Result %s has bad type.  Aborting value filling to avoid crash.", id);
            break;
        }
        line->has_value = TRUE;
        line->has_error = with_errors;
    }
    results->formatted_are_valid = FALSE;
}

/**
 * gwy_results_fill_values:
 * @results: A set of reported scalar values.
 * @...: Sequence of id, value pairs specifying the individual values, terminated by %NULL.
 *
 * Fills the numerical values in a set of reported scalar values.
 *
 * The types must correspond to the types given upon construction.
 *
 * This function cannot be used for values created with gwy_results_add_format().  Use gwy_results_fill_format().
 *
 * If a value was previously set to N.A. using gwy_results_set_na(), filling its value removes this status.
 *
 * Since: 2.50
 **/
void
gwy_results_fill_values(GwyResults *results,
                        ...)
{
    va_list ap;

    g_return_if_fail(GWY_IS_RESULTS(results));
    va_start(ap, results);
    fill_values(results->priv, FALSE, ap);
    va_end(ap);
}

/**
 * gwy_results_fill_values_with_errors:
 * @results: A set of reported scalar values.
 * @...: Sequence of id, value, error triples specifying the individual values, terminated by %NULL.
 *
 * Fills the numerical values and their errors in a set of reported scalar values.
 *
 * Do not set errors for yes/no values; it makes no sense.
 *
 * See gwy_results_fill_values() for further remarks.
 *
 * Since: 2.50
 **/
void
gwy_results_fill_values_with_errors(GwyResults *results,
                                    ...)
{
    va_list ap;

    g_return_if_fail(GWY_IS_RESULTS(results));
    va_start(ap, results);
    fill_values(results->priv, TRUE, ap);
    va_end(ap);
}

/**
 * gwy_results_fill_format:
 * @results: A set of reported scalar values.
 * @id: Value identifier.
 * @...: List of names (string) and values (corresponding data type), terminated by %NULL.
 *
 * Fills the values for a formatted line in a set of reported scalar values.
 *
 * Even though the value is not atomic and may contain several individual fields, they have to be filled all in one
 * function call.
 *
 * Since: 2.50
 **/
void
gwy_results_fill_format(GwyResults *results,
                        const gchar *id,
                        ...)
{
    ResultLine *line;
    ResultValue *value;
    ResultsPriv *priv;
    const gchar *name;
    GQuark quark;
    va_list ap;
    guint i;
    gchar fmtchar;

    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(id);

    priv = results->priv;
    quark = g_quark_from_string(id);
    if (!(line = find_result_line(priv, quark))) {
        g_warning("Cannot find result %s.", id);
        return;
    }
    if (!line->is_format) {
        g_warning("Result %s is not a format.", id);
        return;
    }

    for (i = 0; i < line->value_set.n; i++)
        line->value_set.values[i].is_set = FALSE;

    va_start(ap, id);
    while ((name = va_arg(ap, const gchar*))) {
        value = find_result_value(&line->value_set, line->format, name);
        if (!value) {
            g_warning("Unknown field %s in value %s.  Aborting value filling to avoid crash.", name, id);
            break;
        }
        fmtchar = result_value_get_format_char(value, line->format);
        if (fmtchar == 'v')
            value->value.d = va_arg(ap, gdouble);
        else if (fmtchar == 'i')
            value->value.i = va_arg(ap, gint);
        else if (fmtchar == 'y')
            value->value.yn = va_arg(ap, gint);
        else if (fmtchar == 's')
            gwy_assign_string(&value->value.s, va_arg(ap, gchar*));
        else {
            g_assert_not_reached();
        }
        value->is_set = TRUE;
    }
    va_end(ap);

    line->has_value = TRUE;  /* Although no particular value may exist. */
    line->has_error = FALSE;
    priv->formatted_are_valid = FALSE;
}

/**
 * gwy_results_fill_covariance_matrix:
 * @results: A set of reported scalar values.
 * @id: Value identifier.
 * @fixed_params: Which parameters were fixed (their covariances are excluded in the report).  Can be %NULL if all
 *                parameters were free or you do not care.
 * @covar_matrix: Full covariance matrix (including elements for fixed parameters).
 *
 * Fills a covariance matrix in a set of reported scalar values.
 *
 * The value must have been created using gwy_results_add_covariance_matrix() or gwy_results_add_covariance_matrixv().
 *
 * Matrix @covar_matrix is passed as a lower left triangular part of a symmetric matrix, the same way as #GwyNLFitter
 * returns it.  The number of elements is @n*(@n+1)/2 where @n is the matrix dimension, equal to the number of
 * symbols.  The length of @fixed_params is @n.
 *
 * Since: 2.50
 **/
void
gwy_results_fill_covariance_matrix(GwyResults *results,
                                   const gchar *id,
                                   const gboolean *fixed_params,
                                   const gdouble *covar_matrix)
{
    ResultLine *line;
    GQuark quark;
    guint n;

    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(id);

    quark = g_quark_from_string(id);
    if (!(line = find_result_line(results->priv, quark))) {
        g_warning("Cannot find result %s.", id);
        return;
    }
    if (!line->is_covar_matrix) {
        g_warning("Result %s is not a covariance matrix.", id);
        return;
    }

    line->has_value = TRUE;
    n = line->covar_n;
    gwy_assign(line->covar_matrix, covar_matrix, n*(n+1)/2);
    if (fixed_params)
        gwy_assign(line->fixed_params, fixed_params, n);
    else
        gwy_clear(line->fixed_params, n);
}

/**
 * gwy_results_set_na:
 * @results: A set of reported scalar values.
 * @id: First value identifier to set to N.A.
 * @...: Identifiers of other values to set to N.A., %NULL-terminated.
 *
 * Sets values in reported scalar values to N.A.
 *
 * Such values are excluded from text reports and some representation of N.A. is returned when their formatted values
 * are explicitly requested.
 *
 * The N.A. state is removed simply by filling a value.
 *
 * Since: 2.50
 **/
void
gwy_results_set_na(GwyResults *results,
                   const gchar *id,
                   ...)
{
    ResultLine *line;
    va_list ap;
    GQuark quark;

    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(id);

    va_start(ap, id);
    do {
        quark = g_quark_from_string(id);
        if (!(line = find_result_line(results->priv, quark))) {
            g_warning("Cannot find result %s.", id);
            continue;
        }
        line->has_value = line->has_error = FALSE;
    } while ((id = va_arg(ap, const gchar*)));
    va_end(ap);
    results->priv->formatted_are_valid = FALSE;
}

/**
 * gwy_results_set_nav:
 * @results: A set of reported scalar values.
 * @n: Number of strings in array @id.
 * @id: Array of value identifiers to set to N.A.
 *
 * Sets values in reported scalar values to N.A.
 *
 * See gwy_results_set_na() for discussion.
 *
 * Since: 2.50
 **/
void
gwy_results_set_nav(GwyResults *results,
                    guint n,
                    const gchar **id)
{
    ResultLine *line;
    GQuark quark;
    guint i;

    g_return_if_fail(GWY_IS_RESULTS(results));
    g_return_if_fail(!n || id);
    for (i = 0; i < n; i++) {
        g_return_if_fail(id[i]);
        quark = g_quark_from_string(id[i]);
        if (!(line = find_result_line(results->priv, quark))) {
            g_warning("Cannot find result %s.", id[i]);
            continue;
        }
        line->has_value = line->has_error = FALSE;
    }
    results->priv->formatted_are_valid = FALSE;
}

static void
construct_line_unit(ResultsPriv *results, ResultLine *line)
{
    GwySIUnit *unit = results->unit;

    gwy_si_unit_set_from_string(unit, NULL);
    if (line->power_t && results->tunit)
        gwy_si_unit_power_multiply(unit, 1, results->tunit, line->power_t, unit);
    if (line->power_u && results->uunit)
        gwy_si_unit_power_multiply(unit, 1, results->uunit, line->power_u, unit);
    if (line->power_v && results->vunit)
        gwy_si_unit_power_multiply(unit, 1, results->vunit, line->power_v, unit);
    if (line->power_w && results->wunit)
        gwy_si_unit_power_multiply(unit, 1, results->wunit, line->power_w, unit);
    if (line->power_x && results->xunit)
        gwy_si_unit_power_multiply(unit, 1, results->xunit, line->power_x, unit);
    if (line->power_y && results->yunit)
        gwy_si_unit_power_multiply(unit, 1, results->yunit, line->power_y, unit);
    if (line->power_z && results->zunit)
        gwy_si_unit_power_multiply(unit, 1, results->zunit, line->power_z, unit);
}

/* Remove prefix from messages we do not want translated.  Generally, if something is translatable, we must use one of
 * gwy_sgettext() and this function on it.  If it is not translatable we must use neither. */
static const gchar*
remove_prefix(const gchar *msgid)
{
    const gchar *p;

    p = strrchr(msgid, '|');
    return p ? p+1 : msgid;
}

static const gchar*
maybe_translate_string(const gchar *s,
                       gboolean translate, gboolean translating)
{
    if (!s || !*s)
        return "";
    if (translate && translating)
        return gwy_sgettext(s);
    if (translate)
        return remove_prefix(s);
    return s;
}

static const gchar*
format_yesno(gboolean value, gboolean translating)
{
    if (translating)
        return value ? _("Yes") : _("No");
    return value ? "Yes" : "No";
}

static void
append_gstring(GString *str, GString *toappend)
{
    g_string_append_len(str, toappend->str, toappend->len);
}

static void
format_fixed_param(ResultLine *line, gboolean for_machine)
{
    if (!line->is_fitting_param || line->has_error)
        return;

    g_string_assign(line->error_formatted, maybe_translate_string(N_("(fixed)"), TRUE, !for_machine));
}

static void
parenthesise_gstring(GString *str)
{
    g_string_prepend_c(str, '(');
    g_string_append_c(str, ')');
}

static GwySIUnitFormatStyle
choose_si_unit_format(gboolean for_machine,
                      gboolean for_report,
                      gboolean is_separated)
{
    if (for_machine)
        return GWY_SI_UNIT_FORMAT_PLAIN;
    if (for_report)
        return is_separated ? GWY_SI_UNIT_FORMAT_UNICODE : GWY_SI_UNIT_FORMAT_VFUNICODE;
    return is_separated ? GWY_SI_UNIT_FORMAT_MARKUP : GWY_SI_UNIT_FORMAT_VFMARKUP;
}

static void
format_base_unit(ResultLine *line,
                 gboolean for_machine,
                 gboolean for_report,
                 gboolean is_separated)
{
    ResultsPriv *results = line->parent;
    gchar *s;

    /* Handle fixed units. */
    if (line->unit_str) {
        if (!strlen(line->unit_str))
            return;

        if (!line->units_formatted)
            line->units_formatted = g_string_new(NULL);
        g_string_assign(line->units_formatted,
                        maybe_translate_string(line->unit_str, line->translate_unit, !for_machine));
        return;
    }

    construct_line_unit(results, line);
    if (gwy_si_unit_equal_string(results->unit, NULL))
        return;

    /* FIXME: Employ unicode instead of Pango markup for reports anyway. */
    s = gwy_si_unit_get_string(results->unit, choose_si_unit_format(for_machine, for_report, is_separated));

    if (*s) {
        if (!line->units_formatted)
            line->units_formatted = g_string_new(s);
        else
            g_string_assign(line->units_formatted, s);
    }
    g_free(s);
}

static gdouble
find_format_line_maximum(const ResultLine *line)
{
    const ValueSet *vset = &line->value_set;
    gdouble m = 0.0;
    guint i;

    g_return_val_if_fail(line->is_format, 0.0);

    for (i = 0; i < vset->n; i++) {
        const ResultValue *v = vset->values + i;

        if (!v->is_set)
            continue;
        if (result_value_get_format_char(v, line->format) != 'v')
            continue;
        if (gwy_isnan(v->value.d) || gwy_isinf(v->value.d))
            continue;
        m = fmax(m, fabs(v->value.d));
    }

    return m;
}

static void
construct_value_format(GwySIValueFormat *vf, ResultLine *line,
                       gdouble m, gint digits,
                       gboolean for_machine,
                       gboolean for_report,
                       gboolean is_separated)
{
    ResultsPriv *results = line->parent;
    GwySIUnitFormatStyle style;
    const gchar *sc;
    gchar *s = NULL;

    vf->magnitude = 1.0;
    digits = MAX(digits, line->precision);
    vf->precision = (for_machine ? 5 : digits);
    if (line->unit_str) {
        sc = maybe_translate_string(line->unit_str, line->translate_unit, !for_machine);
        gwy_si_unit_value_format_set_units(vf, sc);
    }
    else if (line->is_angle) {
        if (for_machine)
            gwy_si_unit_value_format_set_units(vf, "rad");
        else {
            gwy_si_unit_value_format_set_units(vf, _("deg"));
            vf->magnitude = G_PI/180.0;
            vf->precision = MAX(2, line->precision-2);
        }
    }
    else if (line->is_percents) {
        if (for_machine)
            gwy_si_unit_value_format_set_units(vf, "");
        else {
            gwy_si_unit_value_format_set_units(vf, "%");
            vf->magnitude = 0.01;
            vf->precision = MAX(3, line->precision-1);
        }
    }
    else {
        construct_line_unit(results, line);
        style = choose_si_unit_format(for_machine, for_report, is_separated);
        if (for_machine) {
            s = gwy_si_unit_get_string(results->unit, style);
            gwy_si_unit_value_format_set_units(vf, s);
            g_free(s);
        }
        else {
            if (gwy_isnan(m) || gwy_isinf(m) || m == 0.0)
                m = 1.0;

            gwy_si_unit_get_format_with_digits(results->unit, style, m, digits, vf);
        }
    }
}

static void
format_value_with_format(ResultLine *line,
                         const gchar *format, ValueSet *vset,
                         GwySIValueFormat *vf, gboolean for_machine)
{
    GString *str = line->value_formatted;
    ResultValue *v, *vprev;
    guint i;
    gchar fmtchar;
    gchar *dbuf = NULL;

    if (!vset->n) {
        g_string_assign(str, format);
        return;
    }

    g_string_truncate(str, 0);
    v = vset->values;
    if (v->fbegin > 0)
        g_string_append_len(str, format, v->fbegin);

    v = NULL;
    for (i = 0; i < vset->n; i++) {
        vprev = v;
        v = vset->values + i;
        if (i > 0) {
            if (v->fbegin > vprev->fbegin + vprev->flen) {
                g_string_append_len(str,
                                    format + vprev->fbegin + vprev->flen,
                                    v->fbegin - (vprev->fbegin + vprev->flen));
            }
        }
        /* TODO: Handle unset values! */

        fmtchar = result_value_get_format_char(v, format);
        if (fmtchar == 's') {
            g_string_append(str, maybe_translate_string(v->value.s, line->translate_strval, !for_machine));
            continue;
        }

        if (fmtchar == 'i') {
            g_string_append_printf(str, "%li", v->value.i);
            continue;
        }

        if (fmtchar == 'y') {
            g_string_append(str, format_yesno(v->value.yn, !for_machine));
            continue;
        }

        g_assert(fmtchar == 'v');
        if (for_machine) {
            if (!dbuf)
                dbuf = g_new(gchar, 64);

            g_ascii_formatd(dbuf, 64, "%g", v->value.d);
            g_string_append(str, dbuf);
        }
        else {
            g_string_append_printf(str, "%.*f", vf->precision, v->value.d/vf->magnitude);
        }

    }
    g_free(dbuf);

    i = strlen(format);
    if (i > v->fbegin + v->flen) {
        g_string_append_len(str,
                            format + v->fbegin + v->flen,
                            i - (v->fbegin + v->flen));
    }

    if (*vf->units) {
        if (!line->units_formatted)
            line->units_formatted = g_string_new(vf->units);
        else
            g_string_assign(line->units_formatted, vf->units);
    }
}

static void
format_all_values(ResultsPriv *results,
                  gboolean for_report, GwyResultsReportType report_type)
{
    GwySIValueFormat *vf;
    ResultLine *line;
    guint gid, i, digits;
    gboolean for_machine = FALSE, is_separated = FALSE;
    gchar *dbuf = NULL;
    gdouble m;

    if (for_report) {
        for_machine = (report_type & GWY_RESULTS_REPORT_MACHINE);
        is_separated = ((report_type & 0xff) != GWY_RESULTS_REPORT_COLON);
    }
    digits = (for_report ? 5 : 3);

    /* Find maximum value in each group to set scale. */
    for (gid = 1; gid <= results->ngroups; gid++)
        results->groups[gid].max = 0.0;

    for (i = 0; i < results->n; i++) {
        line = results->lines + i;
        if (!(gid = line->bind_group))
            continue;
        if (gwy_isnan(line->value.d) || gwy_isinf(line->value.d))
            continue;
        if (!line->has_value)
            continue;

        results->groups[gid].max = fmax(results->groups[gid].max, fabs(line->value.d));
    }
    for (gid = 1; gid <= results->ngroups; gid++) {
        results->groups[gid].seen = FALSE;
        if (!results->groups[gid].vf)
            results->groups[gid].vf = gwy_si_unit_value_format_new(1.0, 4, "");
    }

    /* Format everything.  Set up group value formats the first time we
     * encounter some line from the group. */
    for (i = 0; i < results->n; i++) {
        line = results->lines + i;
        if (line->is_header || line->is_separator || line->is_covar_matrix)
            continue;

        if (!line->value_formatted)
            line->value_formatted = g_string_new(NULL);

        if (line->units_formatted)
            g_string_truncate(line->units_formatted, 0);

        if (line->has_error || line->is_fitting_param) {
            if (!line->error_formatted)
                line->error_formatted = g_string_new(NULL);
        }
        if (line->error_formatted)
            g_string_truncate(line->error_formatted, 0);

        if (!line->has_value) {
            /* FIXME: Is this right? */
            g_string_assign(line->value_formatted, for_machine ? "N.A." : _("N.A."));
            continue;
        }

        if (line->is_format) {
            ValueSet myvset, *vset;
            const gchar *myformat = NULL;

            construct_line_unit(results, line);
            vf = results->vf;
            m = find_format_line_maximum(line);
            construct_value_format(vf, line, m, digits, for_machine, for_report, is_separated);

            if (line->translate_format) {
                gwy_clear(&myvset, 1);
                vset = &myvset;
                myformat = maybe_translate_string(line->format, TRUE, !for_machine);
                parse_format(myformat, vset);
                value_set_copy_values(&line->value_set, vset, line->format, myformat);
            }
            else {
                /* Already parsed. */
                myformat = line->format;
                vset = &line->value_set;
            }

            format_value_with_format(line, myformat, vset, vf, for_machine);
            if (line->translate_format)
                value_set_clear(vset, myformat);
            continue;
        }

        if (line->type == GWY_RESULTS_VALUE_STRING) {
            g_string_assign(line->value_formatted,
                            maybe_translate_string(line->value.s, line->translate_strval, !for_machine));
            if (line->has_error) {
                g_string_assign(line->error_formatted,
                                maybe_translate_string(line->error.s, line->translate_strval, !for_machine));
            }
            format_fixed_param(line, for_machine);
            format_base_unit(line, for_machine, for_report, is_separated);
            continue;
        }

        if (line->type == GWY_RESULTS_VALUE_INT) {
            g_string_printf(line->value_formatted, "%li", line->value.i);
            if (line->has_error)
                g_string_printf(line->error_formatted, "%li", line->error.i);
            format_fixed_param(line, for_machine);
            format_base_unit(line, for_machine, for_report, is_separated);
            continue;
        }

        if (line->type == GWY_RESULTS_VALUE_YESNO) {
            g_string_assign(line->value_formatted, format_yesno(line->value.yn, !for_machine));
            continue;
        }

        g_assert(line->type == GWY_RESULTS_VALUE_FLOAT);

        if ((gid = line->bind_group)) {
            vf = results->groups[gid].vf;
            m = results->groups[gid].max;
        }
        else {
            vf = results->vf;
            m = fabs(line->value.d);
        }

        if (!gid || !results->groups[gid].seen)
            construct_value_format(vf, line, m, digits, for_machine, for_report, is_separated);

        if (gid)
            results->groups[gid].seen = TRUE;

        if (for_machine) {
            if (!dbuf)
                dbuf = g_new(gchar, 64);

            g_ascii_formatd(dbuf, 64, "%g", line->value.d);
            g_string_assign(line->value_formatted, dbuf);
            if (line->has_error) {
                g_ascii_formatd(dbuf, 64, "%g", line->error.d);
                g_string_assign(line->error_formatted, dbuf);
            }
        }
        else {
            g_string_printf(line->value_formatted, "%.*f", vf->precision, line->value.d/vf->magnitude);
            if (line->has_error)
                g_string_printf(line->error_formatted, "%.*f", vf->precision, line->error.d/vf->magnitude);
        }
        format_fixed_param(line, for_machine);

        if (*vf->units) {
            if (!line->units_formatted)
                line->units_formatted = g_string_new(vf->units);
            else
                g_string_assign(line->units_formatted, vf->units);
        }
    }

    g_free(dbuf);

    results->formatted_are_valid = TRUE;
}

static void
append_separator(GString *str, GwyResultsReportType base_type)
{
    if (base_type == GWY_RESULTS_REPORT_TABSEP)
        g_string_append_c(str, '\t');
    if (base_type == GWY_RESULTS_REPORT_CSV)
        g_string_append(str, "\",\"");
}

/* FIXME: There are quite a few other characters we can replace by Unicode. */
static void
append_superscript_unicode(GString *string, const gchar *buf, guint len)
{
    guint i;

    for (i = 0; i < len; i++) {
        if (buf[i] == '0' || (buf[i] >= '4' && buf[i] <= '9'))
            g_string_append_unichar(string, 0x2070 + buf[i] - '0');
        else if (buf[i] == '1')
            g_string_append_len(string, "¹", sizeof("¹")-1);
        else if (buf[i] == '2')
            g_string_append_len(string, "²", sizeof("²")-1);
        else if (buf[i] == '3')
            g_string_append_len(string, "³", sizeof("³")-1);
        else {
            g_warning("Weird digits in superscript %*s\n", len, buf);
            g_string_append_c(string, buf[i]);
        }
    }
}

/* FIXME: There are quite a few other characters we can replace by Unicode. */
static void
append_subscript_unicode(GString *string, const gchar *buf, guint len)
{
    guint i;

    for (i = 0; i < len; i++) {
        if (buf[i] >= '0' && buf[i] <= '9')
            g_string_append_unichar(string, 0x2080 + buf[i] - '0');
        else {
            g_warning("Weird digits in subscript %.*s\n", len, buf);
            g_string_append_c(string, buf[i]);
        }
    }
}

static void
append_approximate_pango_markup(GString *str, const gchar *s)
{
    const gchar *p, *t;

    while ((p = strchr(s, '<'))) {
        g_string_append_len(str, s, p-s);

        if (strncmp(p+1, "sub>", 4) == 0) {
            s = p+5;
            if ((p = strchr(s, '<'))) {
                if (p > s && strncmp(p+1, "/sub>", 5) == 0) {
                    for (t = s; t < p; t++) {
                        if (!g_ascii_isdigit(*t))
                            break;
                    }
                    if (t == p)
                        append_subscript_unicode(str, s, p-s);
                    else
                        g_string_append_len(str, s, p-s);
                    s = p+6;
                    continue;
                }
            }
            /* If we do not hit the inner continue, do not do anything.
             * We have @s positioned after the end of the tag, as we want. */
        }
        else if (strncmp(p+1, "sup>", 4) == 0) {
            s = p+5;
            if ((p = strchr(s, '<'))) {
                if (p > s && strncmp(p+1, "/sup>", 5) == 0) {
                    for (t = s; t < p; t++) {
                        if (!g_ascii_isdigit(*t))
                            break;
                    }
                    if (t == p)
                        append_superscript_unicode(str, s, p-s);
                    else
                        g_string_append_len(str, s, p-s);
                    s = p+6;
                    continue;
                }
            }
            /* If we do not hit the inner continue, do not do anything.
             * We have @s positioned after the end of the tag, as we want. */
        }
        else {
            if (!(s = strchr(p+1, '>'))) {
                g_warning("Invalid Pango markup %s.", p);
                return;
            }
            s++;
        }
    }
    g_string_append(str, s);
}

static void
append_label_with_symbol(GString *str, ResultLine *line,
                         gboolean for_report, gboolean for_machine)
{
    const gchar *s;
    gboolean label_nonempty;

    s = maybe_translate_string(line->label, TRUE, !for_machine);
    label_nonempty = *s;
    if (for_report)
        append_approximate_pango_markup(str, s);
    else
        g_string_append(str, s);

    if (line->symbol && *line->symbol) {
        /* FIXME: This may need some internationalisation love. */
        if (label_nonempty)
            g_string_append(str, " (");

        if (for_report)
            append_approximate_pango_markup(str, line->symbol);
        else
            g_string_append(str, line->symbol);

        if (label_nonempty)
            g_string_append(str, ")");
    }
}

/* This is only for reports and formats the matrix itself (the label was already added). */
static void
append_covariance_matrix(GString *str, ResultLine *line,
                         GwyResultsReportType base_type, gboolean for_machine)
{
    GString *symstr;
    CovarMatrixSymbol *symbols;
    gint *free_param_map;
    guint maxwidth = 6, i, j, fi, fj, pos, n, nfree, wprev;
    gchar *s, *end, *padding = NULL;
    const gchar *label;
    gdouble cij, sigmaisigmaj;
    const gdouble *covar;

    /* The label for covariance matrix works a bit like header. */
    label = maybe_translate_string(line->label, TRUE, !for_machine);
    if (*label) {
        append_approximate_pango_markup(str, label);
        g_string_append_c(str, '\n');
    }

    n = line->covar_n;
    free_param_map = g_new(gint, n);
    nfree = gwy_math_nlfit_map_free_params(line->fixed_params, n, free_param_map);

    /* Use line->value_formatted as a scratch buffer. */
    if (!line->value_formatted)
        line->value_formatted = g_string_new(NULL);
    symstr = line->value_formatted;
    g_string_truncate(symstr, 0);

    /* Split the symbols.  Approximate using Unicode, measure character width
     * of the result. */
    symbols = g_new(CovarMatrixSymbol, nfree);
    pos = 0;
    for (i = j = 0; i < n; i++) {
        s = line->symbol + pos;
        end = strchr(s, '\n');
        g_assert(end);
        if (!line->fixed_params[i]) {
            *end = '\0';
            symbols[j].pos = symstr->len;
            append_approximate_pango_markup(symstr, s);
            symbols[j].len = symstr->len - symbols[j].pos;
            symbols[j].width = gwy_str_fixed_font_width(symstr->str + symbols[j].pos);
            j++;
            g_string_append_c(symstr, '\n');
            *end = '\n';
        }
        pos += end+1 - s;
    }

    /* Coefficient format is -0.999.  So the minimum width is 6 characters, but we must make it wider when the symbols
     * are wider. */
    for (i = 0; i < nfree; i++)
        symstr->str[symbols[i].pos + symbols[i].len] = '\0';

    if (base_type == GWY_RESULTS_REPORT_COLON) {
        for (i = 0; i < nfree; i++) {
            if (symbols[i].width > maxwidth)
                maxwidth = symbols[i].width;
        }
        padding = g_new(gchar, maxwidth+2);
        memset(padding, ' ', maxwidth+1);
        padding[maxwidth+1] = '\0';
    }

    /* The actual formatting.  We must use symbols[i].width for the widths, do not rely on libc string padding! */
    covar = line->covar_matrix;
    for (i = 0; i < nfree; i++) {
        /* Symbol at the beginning. */
        g_string_append(str, symstr->str + symbols[i].pos);
        if (base_type == GWY_RESULTS_REPORT_COLON)
            g_string_append_len(str, padding, maxwidth+1 - symbols[i].width);
        else
            append_separator(str, base_type);

        /* Values. */
        fi = free_param_map[i];
        for (j = 0; j <= i; j++) {
            fj = free_param_map[j];
            cij = SLi(covar, fi, fj);
            sigmaisigmaj = sqrt(SLi(covar, fi, fi) * SLi(covar, fj, fj));
            if (gwy_isnan(sigmaisigmaj) || sigmaisigmaj == 0.0) {
                g_warning("Invalid covariance matrix diagonal element.");
                cij = 0.0;
            }
            else
                cij /= sigmaisigmaj;

            if (for_machine) {
                gchar dbuf[8];

                g_ascii_formatd(dbuf, 8, "% .03f", cij);
                g_string_append(str, dbuf);
            }
            else
                g_string_append_printf(str, "% .03f", cij);

            if (j < i) {
                if (base_type == GWY_RESULTS_REPORT_COLON)
                    g_string_append_len(str, padding, maxwidth+1 - 6);
                else
                    append_separator(str, base_type);
            }
        }
        g_string_append_c(str, '\n');
    }

    /* The final line with symbols. */
    wprev = 0;
    for (i = 0; i < nfree; i++) {
        if (base_type == GWY_RESULTS_REPORT_COLON)
            g_string_append_len(str, padding, maxwidth+1 - wprev);
        else
            append_separator(str, base_type);

        g_string_append(str, symstr->str + symbols[i].pos);
        wprev = symbols[i].width;
    }
    g_string_append_c(str, '\n');

    g_free(padding);
    g_free(symbols);
    g_free(free_param_map);
}

/**
 * gwy_results_create_report:
 * @results: A set of reported scalar values.
 * @report_type: Requested report style.
 *
 * Formats a text report for a set of scalar values.
 *
 * Value lines that have never been filled or were declared N.A. using gwy_results_set_na() are skipped in the report.
 *
 * Returns: A newly allocated string with the report.  The caller must free it with g_free().
 *
 * Since: 2.50
 **/
gchar*
gwy_results_create_report(GwyResults *results,
                          GwyResultsReportType report_type)
{
    GwyResultsReportType base_type = (report_type & 0xff);
    gboolean for_machine = (report_type & GWY_RESULTS_REPORT_MACHINE);
    gboolean is_separated = (base_type != GWY_RESULTS_REPORT_COLON);
    ResultsPriv *priv;
    GString *str;
    ResultLine *line;
    guint i, maxwidth, pos;
    gchar *padding = NULL;
    gboolean last_was_separator;

    g_return_val_if_fail(GWY_IS_RESULTS(results), NULL);

    priv = results->priv;
    str = priv->str;

    /* Find maximum label width for padding. */
    maxwidth = 0;
    if (base_type == GWY_RESULTS_REPORT_COLON) {
        for (i = 0; i < priv->n; i++) {
            line = priv->lines + i;
            if (line->is_separator || line->is_covar_matrix)
                continue;
            if (line->is_header && !line->has_value)
                continue;
            if (!line->label)
                continue;

            g_string_truncate(str, 0);
            append_label_with_symbol(str, line, TRUE, for_machine);
            line->label_width = gwy_str_fixed_font_width(str->str);
            maxwidth = MAX(maxwidth, line->label_width);
        }
        padding = g_new(gchar, maxwidth+3);
        memset(padding, ' ', maxwidth+2);
        padding[maxwidth+2] = '\0';
    }

    /* Do the actual report formatting. */
    format_all_values(priv, TRUE, report_type);
    g_string_truncate(str, 0);
    last_was_separator = FALSE;
    for (i = 0; i < priv->n; i++) {
        line = priv->lines + i;
        if (line->is_separator) {
            last_was_separator = TRUE;
            continue;
        }
        if (!line->is_header && !line->has_value)
            continue;

        /* Cannot format covariance matrix to CSV. */
        if (line->is_covar_matrix && base_type == GWY_RESULTS_REPORT_CSV)
            continue;

        if (last_was_separator) {
            /* Do not emit silly looking empty records in CSV. */
            if (base_type != GWY_RESULTS_REPORT_CSV)
                g_string_append_c(str, '\n');
            last_was_separator = FALSE;
        }

        if (line->is_covar_matrix) {
            append_covariance_matrix(str, line, base_type, for_machine);
            continue;
        }

        if (base_type == GWY_RESULTS_REPORT_CSV)
            g_string_append_c(str, '"');

        /* FIXME: quote " in CSV strings by replacing it with "".  Also try not
         * to quote empty fields because that produces ""... */
        append_label_with_symbol(str, line, TRUE, for_machine);
        if (line->is_header) {
            /* CSV must have always the same number of columns, i.e. four. */
            if (base_type == GWY_RESULTS_REPORT_CSV)
                g_string_append(str, "\",,,");
            g_string_append_c(str, '\n');
            continue;
        }

        if (is_separated)
            append_separator(str, base_type);
        else {
            /* FIXME: The ": " may need some internationalisation love. */
            if (line->label_width) {
                g_string_append(str, ": ");
                g_string_append_len(str, padding, maxwidth - line->label_width);
            }
            else
                g_string_append_len(str, padding, maxwidth+2);
        }

        pos = str->len;
        append_gstring(str, line->value_formatted);
        if (is_separated) {
            append_separator(str, base_type);
            if (line->has_error || line->is_fitting_param)
                append_gstring(str, line->error_formatted);
            append_separator(str, base_type);
            if (line->units_formatted)
                append_gstring(str, line->units_formatted);
        }
        else {
            if (line->has_error || line->is_fitting_param) {
                g_string_append(str, line->has_error ? " ± " : " ");
                append_gstring(str, line->error_formatted);
            }
            if (line->units_formatted && line->units_formatted->len) {
                if (!for_machine && line->has_error) {
                    g_string_insert_c(str, pos, '(');
                    g_string_append_c(str, ')');
                }
                g_string_append_c(str, ' ');
                append_gstring(str, line->units_formatted);
            }
        }
        if (base_type == GWY_RESULTS_REPORT_CSV)
            g_string_append_c(str, '"');
        g_string_append_c(str, '\n');
    }

    /* We destroyed the formatted values we normally want. */
    priv->formatted_are_valid = FALSE;
    g_free(padding);

    return g_strdup(str->str);
}

/**
 * gwy_results_get_label:
 * @results: A set of reported scalar values.
 * @id: Value identifier.
 *
 * Gets the label corresponding to a value in a set of reported scalar values.
 *
 * Returns: A string owned by @results.
 *
 * Since: 2.50
 **/
const gchar*
gwy_results_get_label(GwyResults *results, const gchar *id)
{
    ResultLine *line;
    GQuark quark;

    g_return_val_if_fail(GWY_IS_RESULTS(results), "");
    g_return_val_if_fail(id, "");
    quark = g_quark_from_string(id);
    if (!(line = find_result_line(results->priv, quark))) {
        g_warning("Cannot find result %s.", id);
        return "";
    }
    return line->label;
}

/**
 * gwy_results_get_symbol:
 * @results: A set of reported scalar values.
 * @id: Value identifier.
 *
 * Gets the symbol corresponding to a value in a set of reported scalar values.
 *
 * Returns: A string owned by @results.  An empty string is returned when the value has no symbol.
 *
 * Since: 2.50
 **/
const gchar*
gwy_results_get_symbol(GwyResults *results, const gchar *id)
{
    ResultLine *line;
    GQuark quark;

    g_return_val_if_fail(GWY_IS_RESULTS(results), "");
    g_return_val_if_fail(id, "");
    quark = g_quark_from_string(id);
    if (!(line = find_result_line(results->priv, quark))) {
        g_warning("Cannot find result %s.", id);
        return "";
    }
    return line->symbol ? line->symbol : "";
}

/**
 * gwy_results_get_label_with_symbol:
 * @results: A set of reported scalar values.
 * @id: Value identifier.
 *
 * Gets the label and symbol corresponding to a value in a set of reported scalar values.
 *
 * Returns: A string owned by @results.
 *
 * Since: 2.50
 **/
const gchar*
gwy_results_get_label_with_symbol(GwyResults *results, const gchar *id)
{
    ResultLine *line;
    GQuark quark;
    GString *str;

    g_return_val_if_fail(GWY_IS_RESULTS(results), "");
    g_return_val_if_fail(id, "");
    quark = g_quark_from_string(id);
    if (!(line = find_result_line(results->priv, quark))) {
        g_warning("Cannot find result %s.", id);
        return "";
    }
    str = results->priv->str;
    g_string_truncate(str, 0);
    append_label_with_symbol(str, line, FALSE, FALSE);
    return str->str;
}

static ResultLine*
results_get_formatted_common(GwyResults *results, const gchar *id)
{
    ResultLine *line;
    GQuark quark;

    g_return_val_if_fail(GWY_IS_RESULTS(results), NULL);
    g_return_val_if_fail(id, NULL);
    quark = g_quark_from_string(id);
    if (!(line = find_result_line(results->priv, quark))) {
        g_warning("Cannot find result %s.", id);
        return NULL;
    }
    if (line->is_covar_matrix) {
        g_warning("Cannot format covariance matrix as a scalar.");
        return NULL;
    }

    if (!results->priv->formatted_are_valid)
        format_all_values(results->priv, FALSE, GWY_RESULTS_REPORT_COLON);

    g_string_truncate(results->priv->str, 0);

    return line;
}

/**
 * gwy_results_get_value:
 * @results: A set of reported scalar values.
 * @id: Value identifier.
 *
 * Provides one formatted value in a set of reported scalar values.
 *
 * This function returns just the value, without error or units.
 *
 * Returns: A string owned by @results.  It must not be modified.  It is only valid until another @results method
 *          call.
 *
 * Since: 2.50
 **/
const gchar*
gwy_results_get_value(GwyResults *results, const gchar *id)
{
    ResultLine *line;
    GString *str;

    if (!(line = results_get_formatted_common(results, id)))
        return "";
    str = results->priv->str;
    if (line->value_formatted && line->value_formatted->len)
        append_gstring(str, line->value_formatted);
    return str->str;
}

/**
 * gwy_results_get_error:
 * @results: A set of reported scalar values.
 * @id: Value identifier.
 *
 * Provides one formatted error in a set of reported scalar values.
 *
 * This function returns just the error, without value or units.
 *
 * Returns: A string owned by @results.  It must not be modified.  It is only valid until another @results method
 *          call.
 *
 * Since: 2.50
 **/
const gchar*
gwy_results_get_error(GwyResults *results, const gchar *id)
{
    ResultLine *line;
    GString *str;

    if (!(line = results_get_formatted_common(results, id)))
        return "";
    str = results->priv->str;
    if (line->error_formatted && line->error_formatted->len)
        append_gstring(str, line->error_formatted);
    return str->str;
}

/**
 * gwy_results_get_value_with_error:
 * @results: A set of reported scalar values.
 * @id: Value identifier.
 *
 * Provides one formatted value with error in a set of reported scalar values.
 *
 * This function returns the value with error, but without units.  If there is no error the returned string is
 * identical to what gwy_results_get_value() returns.
 *
 * Returns: A string owned by @results.  It must not be modified.  It is only valid until another @results method
 *          call.
 *
 * Since: 2.50
 **/
const gchar*
gwy_results_get_value_with_error(GwyResults *results, const gchar *id)
{
    ResultLine *line;
    GString *str;

    if (!(line = results_get_formatted_common(results, id)))
        return "";
    str = results->priv->str;
    if (line->value_formatted && line->value_formatted->len)
        append_gstring(str, line->value_formatted);
    if (line->error_formatted && line->error_formatted->len) {
        g_string_append(str, " ± ");
        append_gstring(str, line->error_formatted);
        /* Keep the parenthetisation here? */
        if (line->units_formatted && line->units_formatted->len)
            parenthesise_gstring(str);
    }

    return str->str;
}

/**
 * gwy_results_get_units:
 * @results: A set of reported scalar values.
 * @id: Value identifier.
 *
 * Provides formatted units in a set of reported scalar values.
 *
 * This function returns just the units, without value.
 *
 * Returns: A string owned by @results.  It must not be modified.  It is only valid until another @results method
 *          call.
 *
 * Since: 2.50
 **/
const gchar*
gwy_results_get_units(GwyResults *results, const gchar *id)
{
    ResultLine *line;
    GString *str;

    if (!(line = results_get_formatted_common(results, id)))
        return "";
    str = results->priv->str;
    if (line->units_formatted && line->units_formatted->len)
        append_gstring(str, line->units_formatted);
    return str->str;
}

/**
 * gwy_results_get_full:
 * @results: A set of reported scalar values.
 * @id: Value identifier.
 *
 * Provides formatted value with units in a set of reported scalar values.
 *
 * This function returns the entire formatted value.
 *
 * Returns: A string owned by @results.  It must not be modified.  It is only valid until another @results method
 *          call.
 *
 * Since: 2.50
 **/
const gchar*
gwy_results_get_full(GwyResults *results, const gchar *id)
{
    ResultLine *line;
    GString *str;

    if (!(line = results_get_formatted_common(results, id)))
        return "";
    str = results->priv->str;
    if (line->value_formatted && line->value_formatted->len)
        append_gstring(str, line->value_formatted);
    if (line->error_formatted && line->error_formatted->len) {
        g_string_append(str, " ± ");
        append_gstring(str, line->error_formatted);
        if (line->units_formatted && line->units_formatted->len)
            parenthesise_gstring(str);
    }
    if (line->units_formatted && line->units_formatted->len) {
        g_string_append_c(str, ' ');
        append_gstring(str, line->units_formatted);
    }

    return str->str;
}

/**
 * gwy_results_value_is_na:
 * @results: A set of reported scalar values.
 * @id: Value identifier.
 *
 * Checks whether a value in a set of reported scalar values is invalid.
 *
 * A value is valid if it was set to a finite value and was has not been made N.A. using gwy_results_set_na() since.
 * Otherwise it is invalid and this function returns %TRUE.
 *
 * Returns: %TRUE if the value is invalid.
 *
 * Since: 2.50
 **/
gboolean
gwy_results_value_is_na(GwyResults *results, const gchar *id)
{
    ResultLine *line;
    GQuark quark;

    g_return_val_if_fail(GWY_IS_RESULTS(results), TRUE);
    g_return_val_if_fail(id, TRUE);
    quark = g_quark_from_string(id);
    if (!(line = find_result_line(results->priv, quark))) {
        g_warning("Cannot find result %s.", id);
        return TRUE;
    }
    return !line->has_value;
}

static void
format_one_value(GString *str, gdouble v,
                 GwyResultsReportType base_type, gboolean for_machine)
{
    if (gwy_isnan(v) || gwy_isinf(v)) {
        if (base_type != GWY_RESULTS_REPORT_CSV)
            g_string_append(str, "---");
    }
    else {
        if (for_machine) {
            gchar dbuf[64];

            g_ascii_formatd(dbuf, 64, "%g", v);
            g_string_append(str, dbuf);
        }
        else
            g_string_append_printf(str, "%.8f", v);
    }
}

/**
 * gwy_format_result_table_row:
 * @str: String to append formatted row to.
 * @report_type: Requested report style.
 * @n: Number of values.
 * @...: Sequence of @n #gdouble values to format.
 *
 * Formats a row of tabular results.
 *
 * This is a helper function for formatting tabular results.  It appends formatted sequence of real numbers to the
 * string @str and adds a newline.
 *
 * Only %GWY_RESULTS_REPORT_TABSEP and %GWY_RESULTS_REPORT_CSV are valid styles for tabular data.
 *
 * When the %GWY_RESULTS_REPORT_MACHINE flag is included, the caller should generally also pass the values in base
 * units (without any powers of 10), although this function has no means to actually enforce it.
 *
 * Since: 2.50
 **/
void
gwy_format_result_table_row(GString *str,
                            GwyResultsReportType report_type,
                            guint n,
                            ...)
{
    GwyResultsReportType base_type = (report_type & 0xff);
    gboolean for_machine = (report_type & GWY_RESULTS_REPORT_MACHINE);
    gdouble v;
    va_list ap;
    guint i;

    g_return_if_fail(str);
    g_return_if_fail(base_type != GWY_RESULTS_REPORT_COLON);

    if (n) {
        if (base_type == GWY_RESULTS_REPORT_CSV)
            g_string_append_c(str, '"');

        va_start(ap, n);
        v = va_arg(ap, gdouble);
        format_one_value(str, v, base_type, for_machine);
        for (i = 1; i < n; i++) {
            append_separator(str, base_type);
            v = va_arg(ap, gdouble);
            format_one_value(str, v, base_type, for_machine);
        }
        va_end(ap);

        if (base_type == GWY_RESULTS_REPORT_CSV)
            g_string_append_c(str, '"');
    }

    g_string_append_c(str, '\n');
}

/**
 * gwy_format_result_table_rowv:
 * @str: String to append formatted row to.
 * @report_type: Requested report style.
 * @n: Number of values.
 * @values: Array of @n real numbers to format.
 *
 * Formats a row of tabular results.
 *
 * This is a helper function for formatting tabular results.  It appends formatted sequence of real numbers to the
 * string @str and adds a newline.
 *
 * See gwy_format_result_table_row() for discussion.
 *
 * Since: 2.50
 **/
void
gwy_format_result_table_rowv(GString *str,
                             GwyResultsReportType report_type,
                             guint n,
                             const gdouble *values)
{
    GwyResultsReportType base_type = (report_type & 0xff);
    gboolean for_machine = (report_type & GWY_RESULTS_REPORT_MACHINE);
    guint i;

    g_return_if_fail(str);
    g_return_if_fail(!n || values);
    g_return_if_fail(base_type != GWY_RESULTS_REPORT_COLON);

    if (n) {
        if (base_type == GWY_RESULTS_REPORT_CSV)
            g_string_append_c(str, '"');

        format_one_value(str, values[0], base_type, for_machine);
        for (i = 1; i < n; i++) {
            append_separator(str, base_type);
            format_one_value(str, values[i], base_type, for_machine);
        }

        if (base_type == GWY_RESULTS_REPORT_CSV)
            g_string_append_c(str, '"');
    }

    g_string_append_c(str, '\n');
}

/**
 * gwy_format_result_table_strings:
 * @str: String to append formatted header row to.
 * @report_type: Requested report style.
 * @n: Number of values.
 * @...: Sequence of @n strings to format.
 *
 * Formats a row of string tabular results.
 *
 * This is a helper function for formatting tabular results, usually used for table headers.
 *
 * It appends the strings delimited with tabulators or as CSV to the string @str and adds a newline. Only
 * %GWY_RESULTS_REPORT_TABSEP and %GWY_RESULTS_REPORT_CSV are valid styles for tabular data.
 *
 * The %GWY_RESULTS_REPORT_MACHINE flags has no effect.
 *
 * Since: 2.50
 **/
void
gwy_format_result_table_strings(GString *str,
                                GwyResultsReportType report_type,
                                guint n,
                                ...)
{
    GwyResultsReportType base_type = (report_type & 0xff);
    const gchar *v;
    va_list ap;
    guint i;

    g_return_if_fail(str);
    g_return_if_fail(base_type != GWY_RESULTS_REPORT_COLON);

    if (n) {
        if (base_type == GWY_RESULTS_REPORT_CSV)
            g_string_append_c(str, '"');

        va_start(ap, n);
        v = va_arg(ap, const gchar*);
        g_string_append(str, v);
        for (i = 1; i < n; i++) {
            append_separator(str, base_type);
            v = va_arg(ap, const gchar*);
            g_string_append(str, v);
        }
        va_end(ap);

        if (base_type == GWY_RESULTS_REPORT_CSV)
            g_string_append_c(str, '"');
    }

    g_string_append_c(str, '\n');
}

/**
 * gwy_format_result_table_stringsv:
 * @str: String to append formatted header row to.
 * @report_type: Requested report style.
 * @n: Number of values.
 * @values: Array of @n strings to format.
 *
 * Formats a row of string tabular results.
 *
 * This is a helper function for formatting tabular results, usually used for table headers.
 *
 * It appends the strings delimited with tabulators or as CSV to the string @str and adds a newline. Only
 * %GWY_RESULTS_REPORT_TABSEP and %GWY_RESULTS_REPORT_CSV are valid styles for tabular data.
 *
 * The %GWY_RESULTS_REPORT_MACHINE flag has no effect.
 *
 * Since: 2.50
 **/
void
gwy_format_result_table_stringsv(GString *str,
                                 GwyResultsReportType report_type,
                                 guint n,
                                 const gchar **values)
{
    GwyResultsReportType base_type = (report_type & 0xff);
    guint i;

    g_return_if_fail(str);
    g_return_if_fail(!n || values);
    g_return_if_fail(base_type != GWY_RESULTS_REPORT_COLON);

    if (n) {
        if (base_type == GWY_RESULTS_REPORT_CSV)
            g_string_append_c(str, '"');

        g_string_append(str, values[0]);
        for (i = 1; i < n; i++) {
            append_separator(str, base_type);
            g_string_append(str, values[i]);
        }

        if (base_type == GWY_RESULTS_REPORT_CSV)
            g_string_append_c(str, '"');
    }

    g_string_append_c(str, '\n');
}

/**
 * gwy_format_result_table_mixed:
 * @str: String to append formatted row to.
 * @report_type: Requested report style.
 * @fields: String listing the @n fields in the row, which must be one of "isvy", as described in
 *          gwy_results_add_format().
 * @...: Sequence of @n values to format, of types corresponding to @fields.
 *
 * This is a helper function for formatting tabular results.  It appends formatted sequence of values to the string
 * @str and adds a newline.
 *
 * Only %GWY_RESULTS_REPORT_TABSEP and %GWY_RESULTS_REPORT_CSV are valid styles for tabular data.
 *
 * When the %GWY_RESULTS_REPORT_MACHINE flag is included, the caller should generally also pass floating point values
 * in base units (without any powers of 10), although this function has no means to actually enforce it.
 *
 * Since: 2.54
 **/
void
gwy_format_result_table_mixed(GString *str,
                              GwyResultsReportType report_type,
                              const gchar *fields,
                              ...)
{
    GwyResultsReportType base_type = (report_type & 0xff);
    gboolean for_machine = (report_type & GWY_RESULTS_REPORT_MACHINE);
    va_list ap;
    gchar fmtchar;
    guint n, i;

    g_return_if_fail(str);
    g_return_if_fail(fields);
    g_return_if_fail(base_type != GWY_RESULTS_REPORT_COLON);

    n = strlen(fields);
    if (n) {
        if (base_type == GWY_RESULTS_REPORT_CSV)
            g_string_append_c(str, '"');

        va_start(ap, fields);
        for (i = 0; i < n; i++) {
            fmtchar = fields[i];
            if (i)
                append_separator(str, base_type);

            if (fmtchar == 'v') {
                gdouble v = va_arg(ap, gdouble);
                format_one_value(str, v, base_type, for_machine);
            }
            else if (fmtchar == 'i') {
                gint v = va_arg(ap, gint);
                g_string_append_printf(str, "%i", v);
            }
            else if (fmtchar == 's') {
                const gchar *v = va_arg(ap, const gchar*);
                g_string_append(str, v);
            }
            else if (fmtchar == 'y') {
                gint v = va_arg(ap, gint);
                g_string_append(str, format_yesno(v, !for_machine));
            }
            else {
                g_assert_not_reached();
            }
        }
        va_end(ap);

        if (base_type == GWY_RESULTS_REPORT_CSV)
            g_string_append_c(str, '"');
    }

    g_string_append_c(str, '\n');
}

/************************** Documentation ****************************/

/**
 * SECTION:gwyresults
 * @title: GwyResults
 * @short_description: Reported set of scalar results
 *
 * #GwyResults is a helper for formatting ordered sets of scalar values for display and reports.
 *
 * After creation, add rows with values to the report with functions such as gwy_results_add_value().  Simplified
 * functions exists for common cases.  A value which have the dimension of @x can be added using
 * gwy_results_add_value_x().  There are functions for angles, percentages, yes/no values, etc. and also report
 * headers (sections) and separators.
 *
 * Each value has a string @id which can be used later to refer to it in functions such as gwy_results_set_na()
 * (marking a value not available) or gwy_results_get_value() (getting formatted value for display).
 *
 * Normal values also have units.  You can give a fixed unit string using the "unit-str" parameter of
 * gwy_results_add_value().  However, usually one specifies just the dimensionality by passing "power-x", "power-y",
 * etc. parameters (or implicitly using function like gwy_results_add_value_x()). The units of @x are then set with
 * gwy_results_set_unit() with unit name "x" and #GwyResults derives the units of individual values itself.
 *
 * Once #GwyResults is set up, numeric values can be filled using gwy_results_fill_values() or its alternatives.  You
 * can then obtain individual values formatted for display using gwy_results_get_value(), gwy_results_get_full(), etc.
 * Or you can format an entire report with all the values using gwy_results_create_report() -- this is useful for
 * saving the report to a text file.  This can be repeated any number of times.
 *
 * The labels passed during construction must persist through the #GwyResults lifetime.  They should be essentially
 * always static translatable strings marked with N_(), but left untranslated.
 *
 * Filling values is relatively cheap and does not invoke any formatting. Formatting only happens when strings or
 * reports are actually requested, and then all values are formatted at once (and remembered).  Therefore, an
 * efficient usage of #GwyResults is to fill all values first and then request whichever formatted values you want.
 * Do not set and format values one by one (it would also prevent format binding from working in any useful way).
 **/

/**
 * GwyResultsValueType:
 * @GWY_RESULTS_VALUE_FLOAT: Floating point number (#gdouble).
 * @GWY_RESULTS_VALUE_STRING: String (#gchar*).
 * @GWY_RESULTS_VALUE_INT: Integer (#gint).
 * @GWY_RESULTS_VALUE_YESNO: Yes/no value (#gboolean).
 *
 * Type of a scalar value in #GwyResults.
 *
 * Yes/no values never have units.  Strings and integers can have units, if you specify them.  The units are always
 * formatted as base (without any power of 10 prefix).
 *
 * Since: 2.50
 **/

/**
 * GwyResultsReportType:
 * @GWY_RESULTS_REPORT_MACHINE: Machine-readable report with base units, C number format (decimal dot) and
 *                              untranslated labels.  This is a flag and can be combined with other values.
 * @GWY_RESULTS_REPORT_COLON: Classic export with labels followed by colons and values (possibly errors) with units,
 *                            aligned for readability.
 * @GWY_RESULTS_REPORT_TABSEP: Four tab-separated columns: label, value, error and units.
 * @GWY_RESULTS_REPORT_CSV: Four comma-separated quoted columns label, value, error and units.
 *
 * Style options for a text report created with #GwyResults.
 *
 * Multi-value lines created with gwy_results_add_format() are also split into four parts with
 * %GWY_RESULTS_REPORT_TABSEP and %GWY_RESULTS_REPORT_CSV. The value part corresponds to the format and thus can be
 * complicated. The error part is always empty.
 *
 * Since: 2.50
 **/

/**
 * GwyResults:
 *
 * #GwyResults is an opaque data structure and should be only manipulated with the functions below.
 *
 * Since: 2.50
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
