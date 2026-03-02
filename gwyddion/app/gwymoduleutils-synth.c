/*
 *  $Id: gwymoduleutils-synth.c 25119 2022-10-20 15:45:17Z yeti-dn $
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

#include "config.h"
#include <libprocess/datafield.h>
#include <libprocess/stats.h>
#include <app/data-browser.h>
#include <app/undo.h>
#include <app/wait.h>
#include <app/log.h>
#include "gwymoduleutils-synth.h"
#include "param-internal.h"

/**
 * gwy_synth_define_dimensions_params:
 * @paramdef: A set of parameter definitions.
 * @first_id: First free id for the block of dimension parameters.
 *
 * Defines the standard set of dimension parameters for a data synthesis module.
 *
 * Parameters with ids starting from @first_id will become #GwySynthDimsParam paramers.  For instance
 * @first_id + %GWY_DIMS_PARAMS_XREAL will be the id of the standard xreal parameter.
 *
 * Since: 2.59
 **/
void
gwy_synth_define_dimensions_params(GwyParamDef *paramdef,
                                   gint first_id)
{
    g_return_if_fail(GWY_IS_PARAM_DEF(paramdef));
    g_return_if_fail(first_id >= 0);

    gwy_param_def_add_int(paramdef, first_id + GWY_DIMS_PARAM_XRES, "dims/xres", _("Horizontal size"), 2, 32768, 256);
    gwy_param_def_add_int(paramdef, first_id + GWY_DIMS_PARAM_YRES, "dims/yres", _("Vertical size"), 2, 32768, 256);
    gwy_param_def_add_boolean(paramdef, first_id + GWY_DIMS_PARAM_SQUARE_IMAGE, "dims/square_image",
                              _("S_quare image"), TRUE);
    gwy_param_def_add_double(paramdef, first_id + GWY_DIMS_PARAM_XREAL, "dims/xreal", _("_Width"), 1e-3, 1e4, 1.0);
    gwy_param_def_add_double(paramdef, first_id + GWY_DIMS_PARAM_YREAL, "dims/yreal", _("_Height"), 1e-3, 1e4, 1.0);
    gwy_param_def_add_boolean(paramdef, first_id + GWY_DIMS_PARAM_SQUARE_PIXELS, "dims/square_pixels",
                              _("_Square pixels"), TRUE);
    gwy_param_def_add_unit(paramdef, first_id + GWY_DIMS_PARAM_XYUNIT, "dims/xyunit", _("_Dimensions unit"), "m");
    gwy_param_def_add_unit(paramdef, first_id + GWY_DIMS_PARAM_ZUNIT, "dims/zunit", _("_Value unit"), "m");
    gwy_param_def_add_boolean(paramdef, first_id + GWY_DIMS_PARAM_REPLACE, "dims/replace",
                              _("_Replace the current image"), FALSE);
    gwy_param_def_add_boolean(paramdef, first_id + GWY_DIMS_PARAM_INITIALIZE, "dims/initialize",
                              _("_Start from the current image"), FALSE);
}

/**
 * gwy_synth_sanitise_params:
 * @params: A set of parameter values.
 * @first_id: First id of the block of dimension parameters.
 * @template_: Template data field.  Normally it is the current image, or %NULL if there is no current image.
 *
 * Ensures a basic consistency the standard set of dimension parameters for a data synthesis module.
 *
 * This function also remembers @first_id and @template_ for @params.  Other helper functions then do not take these
 * arguments, but you need to call this function to set up the association.  If the module has constrains on possible
 * templates it has to ensure the template is valid; if the template is not valid pass %NULL instead.
 *
 * Since: 2.59
 **/
void
gwy_synth_sanitise_params(GwyParams *params,
                          gint first_id,
                          GwyDataField *template_)
{
    gdouble xreal, yreal;
    gint xres, yres;

    g_return_if_fail(GWY_IS_PARAMS(params));
    g_return_if_fail(first_id >= 0);
    g_return_if_fail(!template_ || GWY_IS_DATA_FIELD(template_));

    g_object_set_data(G_OBJECT(params), "gwy-synth-first-id", GINT_TO_POINTER(first_id));
    g_object_set_data(G_OBJECT(params), "gwy-synth-template", template_);

    /* TODO: If we should use the template and it exists; immediately initialise all the parameter according to the
     * template here. */
    if (template_)
        return;

    xres = gwy_params_get_int(params, first_id + GWY_DIMS_PARAM_XRES);
    yres = gwy_params_get_int(params, first_id + GWY_DIMS_PARAM_YRES);
    xreal = gwy_params_get_double(params, first_id + GWY_DIMS_PARAM_XREAL);
    yreal = gwy_params_get_double(params, first_id + GWY_DIMS_PARAM_YREAL);
    gwy_params_set_boolean(params, first_id + GWY_DIMS_PARAM_SQUARE_IMAGE, xres == yres);
    gwy_params_set_boolean(params, first_id + GWY_DIMS_PARAM_SQUARE_PIXELS,
                           fabs(log((xreal*yres)/(yreal*xres))) <= 1e-9);
    gwy_params_set_boolean(params, first_id + GWY_DIMS_PARAM_REPLACE, FALSE);
    gwy_params_set_boolean(params, first_id + GWY_DIMS_PARAM_INITIALIZE, FALSE);
}

/* Apparently only internal. */
static void
gwy_synth_update_lateral_dimensions(GwyParamTable *partable)
{
    GwyParams *params = gwy_param_table_params(partable);
    gint first_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(params), "gwy-synth-first-id"));
    gint power10xy;
    GwySIUnit *xyunit = gwy_params_get_unit(params, first_id + GWY_DIMS_PARAM_XYUNIT, &power10xy);
    GwySIValueFormat *vf = gwy_si_unit_get_format_for_power10(xyunit, GWY_SI_UNIT_FORMAT_VFMARKUP, power10xy, NULL);

    gwy_param_table_set_unitstr(partable, first_id + GWY_DIMS_PARAM_XREAL, vf->units);
    gwy_param_table_set_unitstr(partable, first_id + GWY_DIMS_PARAM_YREAL, vf->units);
    gwy_si_unit_value_format_free(vf);
}

/**
 * gwy_synth_append_dimensions_to_param_table:
 * @partable: Set of parameter value controls.
 * @flags: Set of flags controlling the available parameters.
 *
 * Appends the standard set of dimension parameters for a data synthesis module to a parameter table.
 *
 * Usually, this is used to fill the content of a ‘Dimensions’ tab of the dialogue.
 *
 * The parameter table must be created for a set of parameters defined with gwy_synth_define_dimensions_params() and
 * set up with gwy_synth_sanitise_params().
 *
 * Since: 2.59
 **/
void
gwy_synth_append_dimensions_to_param_table(GwyParamTable *partable,
                                           GwySynthDimsFlags flags)
{
    GwyParams *params;
    GwyDataField *template_;
    gint first_id, i;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    params = gwy_param_table_params(partable);
    template_ = (GwyDataField*)g_object_get_data(G_OBJECT(params), "gwy-synth-template");
    first_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(params), "gwy-synth-first-id"));

    gwy_param_table_append_header(partable, first_id + GWY_DIMS_HEADER_PIXEL, _("Resolution"));
    gwy_param_table_append_slider(partable, first_id + GWY_DIMS_PARAM_XRES);
    gwy_param_table_slider_set_mapping(partable, first_id + GWY_DIMS_PARAM_XRES, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_set_unitstr(partable, first_id + GWY_DIMS_PARAM_XRES, _("px"));
    gwy_param_table_append_slider(partable, first_id + GWY_DIMS_PARAM_YRES);
    gwy_param_table_slider_set_mapping(partable, first_id + GWY_DIMS_PARAM_YRES, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_set_unitstr(partable, first_id + GWY_DIMS_PARAM_YRES, _("px"));
    gwy_param_table_append_checkbox(partable, first_id + GWY_DIMS_PARAM_SQUARE_IMAGE);

    gwy_param_table_append_header(partable, first_id + GWY_DIMS_HEADER_PHYSICAL, _("Physical Dimensions"));
    gwy_param_table_append_slider(partable, first_id + GWY_DIMS_PARAM_XREAL);
    gwy_param_table_slider_set_mapping(partable, first_id + GWY_DIMS_PARAM_XREAL, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(partable, first_id + GWY_DIMS_PARAM_YREAL);
    gwy_param_table_slider_set_mapping(partable, first_id + GWY_DIMS_PARAM_YREAL, GWY_SCALE_MAPPING_LOG);
    gwy_synth_update_lateral_dimensions(partable);
    gwy_param_table_append_checkbox(partable, first_id + GWY_DIMS_PARAM_SQUARE_PIXELS);

    if ((flags & GWY_SYNTH_FIXED_UNITS) != GWY_SYNTH_FIXED_UNITS)
        gwy_param_table_append_header(partable, first_id + GWY_DIMS_HEADER_UNITS, _("Units"));
    if (!(flags & GWY_SYNTH_FIXED_XYUNIT))
        gwy_param_table_append_unit_chooser(partable, first_id + GWY_DIMS_PARAM_XYUNIT);
    if (!(flags & GWY_SYNTH_FIXED_ZUNIT))
        gwy_param_table_append_unit_chooser(partable, first_id + GWY_DIMS_PARAM_ZUNIT);

    if (template_) {
        gwy_param_table_append_header(partable, first_id + GWY_DIMS_HEADER_CURRENT_IMAGE, _("Current Image"));
        gwy_param_table_append_button(partable, first_id + GWY_DIMS_BUTTON_TAKE, -1, GWY_RESPONSE_SYNTH_TAKE_DIMS,
                                      _("_Take Dimensions from Current Image"));
        gwy_param_table_append_checkbox(partable, first_id + GWY_DIMS_PARAM_REPLACE);
        if (!(flags & GWY_SYNTH_NO_INITIALIZE))
            gwy_param_table_append_checkbox(partable, first_id + GWY_DIMS_PARAM_INITIALIZE);
    }

    for (i = GWY_DIMS_PARAM_XRES; i <= GWY_DIMS_PARAM_INITIALIZE; i++) {
        if (gwy_param_table_exists(partable, first_id + i))
            gwy_param_table_set_no_reset(partable, first_id + i, TRUE);
    }
}

/* XXX In cases like the MFM modules the caller is responsible for pre-filtering the templates.  If the template does
 * not match the fixed units it should behave as if there was no template.  Otherwise we can mess up the units here
 * completely. */
/**
 * gwy_synth_use_dimensions_template:
 * @partable: Set of parameter value controls.
 *
 * Updates a set of dimension parameters for a data synthesis module to match the template.
 *
 * This will result in invocation of #GwyParamTable::param-changed signal with id -1.
 *
 * The parameter table must be created for a set of parameters defined with gwy_synth_define_dimensions_params() and
 * set up with gwy_synth_sanitise_params() – this is where the template to use was defined.
 *
 * Since: 2.59
 **/
void
gwy_synth_use_dimensions_template(GwyParamTable *partable)
{
    GwyParams *params;
    GwyDataField *template_;
    GwySIValueFormat *xyvf = NULL, *zvf = NULL;
    GwySIUnit *xyunit, *zunit;
    gint xres, yres, power10xy, first_id;
    gdouble min, max, m, xreal, yreal;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));

    params = gwy_param_table_params(partable);
    first_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(params), "gwy-synth-first-id"));
    template_ = (GwyDataField*)g_object_get_data(G_OBJECT(params), "gwy-synth-template");
    if (!template_) {
        g_warning("There is not template data field to use.");
        return;
    }

    xres = gwy_data_field_get_xres(template_);
    yres = gwy_data_field_get_yres(template_);
    xreal = gwy_data_field_get_xreal(template_);
    yreal = gwy_data_field_get_yreal(template_);

    if (gwy_param_table_exists(partable, first_id + GWY_DIMS_PARAM_XYUNIT)) {
        xyvf = gwy_data_field_get_value_format_xy(template_, GWY_SI_UNIT_FORMAT_PLAIN, xyvf);
        gwy_param_table_set_string(partable, first_id + GWY_DIMS_PARAM_XYUNIT, xyvf->units);
    }
    xyunit = gwy_params_get_unit(params, first_id + GWY_DIMS_PARAM_XYUNIT, &power10xy);
    xyvf = gwy_si_unit_get_format_for_power10(xyunit, GWY_SI_UNIT_FORMAT_VFMARKUP, power10xy, xyvf);

    /* The update handlers are robust enough to weather the storm if we do not do this (and it was not originally
     * here).  But it is just lots of callbacks and stuff that we can easily avoid. */
     _gwy_param_table_in_update(partable, TRUE);

    if (gwy_param_table_exists(partable, first_id + GWY_DIMS_PARAM_ZUNIT)) {
        gwy_data_field_get_min_max(template_, &min, &max);
        /* The height control has a high precision.  We are more likely to run into trouble when the template is
         * relatively flat. */
        if (max == min) {
            max = ABS(max);
            min = 0.0;
        }
        m = 12.0*(max - min);
        zunit = gwy_data_field_get_si_unit_z(template_);
        zvf = gwy_si_unit_get_format_with_digits(zunit, GWY_SI_UNIT_FORMAT_PLAIN, m, 3, zvf);
        gwy_param_table_set_string(partable, first_id + GWY_DIMS_PARAM_ZUNIT, zvf->units);
    }

    gwy_param_table_set_boolean(partable, first_id + GWY_DIMS_PARAM_SQUARE_IMAGE, xres == yres);
    gwy_param_table_set_int(partable, first_id + GWY_DIMS_PARAM_XRES, xres);
    gwy_param_table_set_int(partable, first_id + GWY_DIMS_PARAM_YRES, yres);

    gwy_param_table_set_boolean(partable, first_id + GWY_DIMS_PARAM_SQUARE_PIXELS,
                                fabs(log((xreal*yres)/(yreal*xres))) <= 1e-9);
    gwy_param_table_set_double(partable, first_id + GWY_DIMS_PARAM_XREAL, xreal/xyvf->magnitude);
    gwy_param_table_set_double(partable, first_id + GWY_DIMS_PARAM_YREAL, yreal/xyvf->magnitude);

     _gwy_param_table_in_update(partable, FALSE);

    GWY_SI_VALUE_FORMAT_FREE(xyvf);
    GWY_SI_VALUE_FORMAT_FREE(zvf);

    gwy_param_table_param_changed(partable, -1);
}

/* Apparently only internal. */
static void
gwy_synth_make_dimensions_user_set(GwyParamTable *partable, gboolean user_set)
{
    /* Parameters which are available only for user-set dimensions. */
    static const gint dims_ids[] = {
        GWY_DIMS_HEADER_PIXEL, GWY_DIMS_PARAM_XRES, GWY_DIMS_PARAM_YRES, GWY_DIMS_PARAM_SQUARE_IMAGE,
        GWY_DIMS_HEADER_PHYSICAL, GWY_DIMS_PARAM_XREAL, GWY_DIMS_PARAM_YREAL, GWY_DIMS_PARAM_SQUARE_PIXELS,
        GWY_DIMS_HEADER_UNITS, GWY_DIMS_PARAM_XYUNIT, GWY_DIMS_PARAM_ZUNIT,
        GWY_DIMS_BUTTON_TAKE,
    };
    GwyParams *params = gwy_param_table_params(partable);
    gint first_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(params), "gwy-synth-first-id"));
    guint i;

    for (i = 0; i < G_N_ELEMENTS(dims_ids); i++) {
        if (gwy_param_table_exists(partable, first_id + dims_ids[i]))
            gwy_param_table_set_sensitive(partable, first_id + dims_ids[i], user_set);
    }
}

/**
 * gwy_synth_update_value_unitstrs:
 * @partable: Set of parameter value controls.
 * @ids: Array of parameter ids (controlled by @partable).
 * @nids: The number of items in @ids.
 *
 * Updates unit strings of value-like parameters in a synth module parameter table.
 *
 * The parameters should be free-form value-like parameters, for instance heights.
 *
 * The parameter table must be created for a set of parameters defined with gwy_synth_define_dimensions_params() and
 * set up with gwy_synth_sanitise_params().
 *
 * Since: 2.59
 **/
void
gwy_synth_update_value_unitstrs(GwyParamTable *partable,
                                const gint *ids,
                                guint nids)
{
    GwyParams *params;
    gint power10, first_id;
    GwySIUnit *unit;
    GwySIValueFormat *vf;
    guint i;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    if (!ids || !nids)
        return;

    params = gwy_param_table_params(partable);
    first_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(params), "gwy-synth-first-id"));
    unit = gwy_params_get_unit(params, first_id + GWY_DIMS_PARAM_ZUNIT, &power10);
    vf = gwy_si_unit_get_format_for_power10(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, power10, NULL);
    for (i = 0; i < nids; i++)
        gwy_param_table_set_unitstr(partable, ids[i], vf->units);
    gwy_si_unit_value_format_free(vf);
}

/**
 * gwy_synth_update_lateral_alts:
 * @partable: Set of parameter value controls.
 * @ids: Array of parameter ids (controlled by @partable).
 * @nids: The number of items in @ids.
 *
 * Updates unit strings of dimension-like parameters in a synth module parameter table.
 *
 * The parameters should be lateral pixel dimension parameters with alternative real dimensions.
 *
 * The parameter table must be created for a set of parameters defined with gwy_synth_define_dimensions_params() and
 * set up with gwy_synth_sanitise_params().
 *
 * Since: 2.59
 **/
void
gwy_synth_update_lateral_alts(GwyParamTable *partable,
                              const gint *ids,
                              guint nids)
{
    GwyParams *params;
    gint power10, first_id, xres;
    GwySIUnit *unit;
    gdouble xreal, dx, q;
    GwySIValueFormat *vf;
    guint i;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    if (!ids || !nids)
        return;

    params = gwy_param_table_params(partable);
    first_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(params), "gwy-synth-first-id"));
    unit = gwy_params_get_unit(params, first_id + GWY_DIMS_PARAM_XYUNIT, &power10);
    q = pow10(power10);
    xres = gwy_params_get_int(params, first_id + GWY_DIMS_PARAM_XRES);
    xreal = gwy_params_get_double(params, first_id + GWY_DIMS_PARAM_XREAL)*q;
    dx = xreal/xres;
    vf = gwy_si_unit_get_format_with_resolution(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, xreal, dx, NULL);

    for (i = 0; i < nids; i++) {
        gwy_param_table_set_unitstr(partable, ids[i], _("px"));
        gwy_param_table_alt_set_linear(partable, ids[i], dx/vf->magnitude, 0.0, vf->units);
    }
    gwy_si_unit_value_format_free(vf);
}

/**
 * gwy_synth_update_like_current_button_sensitivity:
 * @partable: Set of parameter value controls.
 * @id: Parameter id corresponding to the Like Current Image button.
 *
 * Updates the sensitivity of the standard Like Current Image button in a synth module parameter table.
 *
 * The buttons is made sensitive if the value units of the template data field match the selected value units. If
 * there is no template the button is usually now show at all.  It is safe to call this function even in this case.
 *
 * The parameter table must be created for a set of parameters defined with gwy_synth_define_dimensions_params() and
 * set up with gwy_synth_sanitise_params().
 *
 * Since: 2.59
 **/
void
gwy_synth_update_like_current_button_sensitivity(GwyParamTable *partable,
                                                 gint id)
{
    GwyParams *params;
    gboolean sens = FALSE;
    gint first_id;
    GwyDataField *template_;

    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));
    if (!gwy_param_table_exists(partable, id))
        return;

    params = gwy_param_table_params(partable);
    first_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(params), "gwy-synth-first-id"));
    template_ = (GwyDataField*)g_object_get_data(G_OBJECT(params), "gwy-synth-template");
    if (template_) {
        GwySIUnit *zunit = gwy_params_get_unit(params, first_id + GWY_DIMS_PARAM_ZUNIT, NULL);
        GwySIUnit *fieldunit = gwy_data_field_get_si_unit_z(template_);
        sens = gwy_si_unit_equal(zunit, fieldunit);
    }
    gwy_param_table_set_sensitive(partable, id, sens);
}

/**
 * gwy_synth_handle_param_changed:
 * @partable: Set of parameter value controls.
 * @id: Changed parameter id (as obtained in the signal handler).
 *
 * Handles changes in a set of standard dimension parameters in a data synthesis module.
 *
 * The parameter table must be created for a set of parameters defined with gwy_synth_define_dimensions_params() and
 * set up with gwy_synth_sanitise_params().
 *
 * Returns: %TRUE if the action taken was a mass parameter update.  The caller should then proceed as if @id was -1,
 *          whether it was originally or not.
 *
 * Since: 2.59
 **/
gboolean
gwy_synth_handle_param_changed(GwyParamTable *partable, gint id)
{
    GwyParams *params;
    gboolean square_image, square_pixels, reset_dims = (id < 0);
    gdouble xreal, yreal;
    gint xres, yres, first_id;

    g_return_val_if_fail(GWY_IS_PARAM_TABLE(partable), FALSE);

    params = gwy_param_table_params(partable);
    first_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(params), "gwy-synth-first-id"));

    if (id < 0 || id == first_id + GWY_DIMS_PARAM_INITIALIZE || id == first_id + GWY_DIMS_PARAM_REPLACE) {
        gboolean do_initialise = (gwy_param_table_exists(partable, first_id + GWY_DIMS_PARAM_INITIALIZE)
                                  ? gwy_params_get_boolean(params, first_id + GWY_DIMS_PARAM_INITIALIZE)
                                  : FALSE);
        gboolean do_replace = gwy_params_get_boolean(params, first_id + GWY_DIMS_PARAM_REPLACE);
        gboolean use_template = (do_replace || do_initialise);

        /* Figure out whether we are adopting template dimensions right now.  If one of the checkboxes is already on
         * and we switch on the second one then nothing changes. */
        if (!(do_initialise && do_replace)) {
            if (id == first_id + GWY_DIMS_PARAM_INITIALIZE && do_initialise)
                reset_dims = TRUE;
            if (id == first_id + GWY_DIMS_PARAM_REPLACE && do_replace)
                reset_dims = TRUE;
        }

        gwy_synth_make_dimensions_user_set(partable, !use_template);
        if (reset_dims && use_template) {
            gwy_synth_use_dimensions_template(partable);
            id = -1;
        }
    }

    if (id < 0 || id == first_id + GWY_DIMS_PARAM_XYUNIT)
        gwy_synth_update_lateral_dimensions(partable);

    square_image = gwy_params_get_boolean(params, first_id + GWY_DIMS_PARAM_SQUARE_IMAGE);
    square_pixels = gwy_params_get_boolean(params, first_id + GWY_DIMS_PARAM_SQUARE_PIXELS);
    xres = gwy_params_get_int(params, first_id + GWY_DIMS_PARAM_XRES);
    yres = gwy_params_get_int(params, first_id + GWY_DIMS_PARAM_YRES);
    /* Here we do not care about id = -1 because in such case either the parameters are initialised correctly or we
     * adopted them from the template, again completely. */
    if (square_image) {
        if (id == first_id + GWY_DIMS_PARAM_YRES)
            gwy_param_table_set_int(partable, first_id + GWY_DIMS_PARAM_XRES, (xres = yres));
        else if (id == first_id + GWY_DIMS_PARAM_XRES
                 || id == first_id + GWY_DIMS_PARAM_SQUARE_IMAGE)
            gwy_param_table_set_int(partable, first_id + GWY_DIMS_PARAM_YRES, (yres = xres));
    }

    if (square_pixels) {
        xreal = gwy_params_get_double(params, first_id + GWY_DIMS_PARAM_XREAL);
        yreal = gwy_params_get_double(params, first_id + GWY_DIMS_PARAM_YREAL);
        if (id == first_id + GWY_DIMS_PARAM_YRES
            || id == first_id + GWY_DIMS_PARAM_SQUARE_IMAGE
            || id == first_id + GWY_DIMS_PARAM_XREAL
            || id == first_id + GWY_DIMS_PARAM_SQUARE_PIXELS) {
            gwy_param_table_set_double(partable, first_id + GWY_DIMS_PARAM_YREAL, (yreal = xreal/xres*yres));
        }
        else if (id == first_id + GWY_DIMS_PARAM_XRES
                 || id == first_id + GWY_DIMS_PARAM_YREAL) {
            gwy_param_table_set_double(partable, first_id + GWY_DIMS_PARAM_XREAL, (xreal = yreal/yres*xres));
        }
    }

    return reset_dims;
}

/**
 * gwy_synth_add_result_to_file:
 * @result: Data field with the simulation output.
 * @data: Data container corresponding to the current file, or %NULL is there is none.
 * @id: Id or the current image (or -1 if there is none).
 * @params: The set of parameter values for the synth module.
 *
 * Adds the result of data synthesis to a file.
 *
 * This function takes care of handling correctly the %GWY_DIMS_PARAM_REPLACE and %GWY_DIMS_PARAM_INITIALIZE options
 * (the latter with regard to logging and sync; actual computation input is handled by the module) in the various
 * cases such as @data and/or @id existing or not.
 *
 * The parameter value set must be created for a set of parameters defined with gwy_synth_define_dimensions_params()
 * and set up with gwy_synth_sanitise_params().
 *
 * Returns: Data id of the result.  It can correspond a newly created image or an existing image, depending on the
 *          settings.
 *
 * Since: 2.59
 **/
GwyAppDataId
gwy_synth_add_result_to_file(GwyDataField *result,
                             GwyContainer *data,
                             gint id,
                             GwyParams *params)
{
    GwyAppDataId dataid = GWY_APP_DATA_ID_NONE;
    gboolean do_replace, do_initialise;
    gint first_id, newid = 0;

    g_return_val_if_fail(GWY_IS_PARAMS(params), dataid);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(result), dataid);

    first_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(params), "gwy-synth-first-id"));
    do_replace = gwy_params_get_boolean(params, first_id + GWY_DIMS_PARAM_REPLACE);
    do_initialise = gwy_params_get_boolean(params, first_id + GWY_DIMS_PARAM_INITIALIZE);

    if (id != -1 && do_replace) {
        GQuark quark = gwy_app_get_data_key_for_id(id);

        gwy_app_undo_qcheckpointv(data, 1, &quark);
        gwy_container_set_object(data, quark, result);
        gwy_app_channel_log_add_proc(data, id, id);
        dataid.datano = gwy_app_data_browser_get_number(data);
        dataid.id = id;

        return dataid;
    }

    if (data) {
        newid = gwy_app_data_browser_add_data_field(result, data, TRUE);
        if (id != -1) {
            gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                    GWY_DATA_ITEM_GRADIENT,
                                    GWY_DATA_ITEM_REAL_SQUARE,
                                    GWY_DATA_ITEM_MASK_COLOR,
                                    0);
        }
    }
    else {
        data = gwy_container_new();
        gwy_container_set_object(data, gwy_app_get_data_key_for_id(newid), result);
        gwy_app_data_browser_add(data);
        gwy_app_data_browser_reset_visibility(data, GWY_VISIBILITY_RESET_SHOW_ALL);
        g_object_unref(data);
    }
    gwy_app_set_data_field_title(data, newid, _("Generated"));
    gwy_app_channel_log_add_proc(data, do_initialise ? id : -1, newid);
    dataid.datano = gwy_app_data_browser_get_number(data);
    dataid.id = newid;

    return dataid;
}

/**
 * gwy_synth_make_result_data_field:
 * @data_field: Data field to be used as template, possibly %NULL.
 * @params: The set of parameter values for the synth module.
 * @always_use_template: %TRUE to always prefer creating a new data field matching @data_field, unless it is %NULL.
 *
 * Creates a data field for the output of a data synthesis module.
 *
 * The new data field properties match either @data_field or values of the dimension and unit parameters, depending
 * on %GWY_DIMS_PARAM_REPLACE and %GWY_DIMS_PARAM_INITIALIZE (and also @always_use_template).
 *
 * The parameter value set must be created for a set of parameters defined with gwy_synth_define_dimensions_params()
 * and set up with gwy_synth_sanitise_params().
 *
 * Returns: A newly created data field.
 *
 * Since: 2.59
 **/
GwyDataField*
gwy_synth_make_result_data_field(GwyDataField *data_field, GwyParams *params,
                                 gboolean always_use_template)
{
    gboolean do_replace, do_initialise;
    gint xres, yres, power10xy, first_id;
    gdouble xreal, yreal, q;
    GwySIUnit *xyunit, *zunit;
    GwyDataField *result;

    g_return_val_if_fail(GWY_IS_PARAMS(params), NULL);
    first_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(params), "gwy-synth-first-id"));

    do_replace = gwy_params_get_boolean(params, first_id + GWY_DIMS_PARAM_REPLACE);
    do_initialise = gwy_params_get_boolean(params, first_id + GWY_DIMS_PARAM_INITIALIZE);

    if (data_field && (always_use_template || do_replace || do_initialise))
        return gwy_data_field_new_alike(data_field, TRUE);

    xres = gwy_params_get_int(params, first_id + GWY_DIMS_PARAM_XRES);
    yres = gwy_params_get_int(params, first_id + GWY_DIMS_PARAM_YRES);
    xreal = gwy_params_get_double(params, first_id + GWY_DIMS_PARAM_XREAL);
    yreal = gwy_params_get_double(params, first_id + GWY_DIMS_PARAM_YREAL);
    xyunit = gwy_params_get_unit(params, first_id + GWY_DIMS_PARAM_XYUNIT, &power10xy);
    zunit = gwy_params_get_unit(params, first_id + GWY_DIMS_PARAM_ZUNIT, NULL);
    q = pow10(power10xy);

    result = gwy_data_field_new(xres, yres, xreal*q, yreal*q, TRUE);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(result), xyunit);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(result), zunit);

    return result;
}

/**
 * gwy_synth_make_preview_data_field:
 * @data_field: Data field to be used as template, possibly %NULL.
 * @size: Pixel size of the result.
 *
 * Creates a suitable data field running a data synthesis module in the preview mode.
 *
 * The preview data field is created by a combination of cutting the central part of @data_field (if too large) and
 * resampling it to higher resolution (if too small).  It will always be square, @size by @size.
 *
 * For some data synthesis modules is can make more sense to run even the preview for the actual image size.  Do not
 * use this function in such case.
 *
 * Returns: A newly created data field.
 *
 * Since: 2.59
 **/
GwyDataField*
gwy_synth_make_preview_data_field(GwyDataField *data_field,
                                  gint size)
{
    GwyDataField *retval;
    gint xres, yres, xoff, yoff;

    xres = gwy_data_field_get_xres(data_field);
    yres = gwy_data_field_get_yres(data_field);

    /* If the field is large enough, just cut an area from the centre. */
    if (xres >= size && yres >= size) {
        xoff = (xres - size)/2;
        yoff = (yres - size)/2;
        return gwy_data_field_area_extract(data_field, xoff, yoff, size, size);
    }

    if (xres <= yres) {
        yoff = (yres - xres)/2;
        data_field = gwy_data_field_area_extract(data_field, 0, yoff, xres, xres);
    }
    else {
        xoff = (xres - yres)/2;
        data_field = gwy_data_field_area_extract(data_field, xoff, 0, yres, yres);
    }

    retval = gwy_data_field_new_resampled(data_field, size, size, GWY_INTERPOLATION_KEY);
    g_object_unref(data_field);

    return retval;
}

/**
 * gwy_synth_update_progress:
 * @timer: A timer started together with the computation.  Pass %NULL to forget the last preview time.
 * @preview_time: Minimum time between previews, in seconds.  Pass a non-positive value if previews are not animated.
 * @i: Current iteration/cycle.  A monotonically increasing number.
 * @niters: The total number of iterations/cycles.
 *
 * Manages progress bar updates and preview animation in a data synthesis module.
 *
 * This helper functions updates the progress bar to @i/@niters and check whether a preview should be done when it is
 * animated.
 *
 * Returns: What should happen.
 *
 * Since: 2.59
 **/
GwySynthUpdateType
gwy_synth_update_progress(GTimer *timer,
                          gdouble preview_time,
                          gulong i,
                          gulong niters)
{
    static gdouble lasttime = 0.0;
    static gdouble lastpreviewtime = 0.0;
    gdouble currtime;

    if (!timer) {
        lasttime = 0.0;
        lastpreviewtime = 0.0;
        return GWY_SYNTH_UPDATE_NOTHING;
    }

    currtime = g_timer_elapsed(timer, NULL);
    if (currtime - lasttime < 0.25)
        return GWY_SYNTH_UPDATE_NOTHING;

    if (!gwy_app_wait_set_fraction((gdouble)i/niters))
        return GWY_SYNTH_UPDATE_CANCELLED;

    lasttime = currtime;
    if (gwy_app_wait_get_enabled() && preview_time > 0.0 && currtime - lastpreviewtime >= preview_time) {
        lastpreviewtime = lasttime;
        return GWY_SYNTH_UPDATE_DO_PREVIEW;
    }

    return GWY_SYNTH_UPDATE_NOTHING;
}

/**
 * SECTION:gwymoduleutils-synth
 * @title: synth module utils
 * @short_description: Helper functions for data synthesis modules
 **/

/**
 * GwySynthResponseType:
 * @GWY_RESPONSE_SYNTH_TAKE_DIMS: Take dimensions and units from the current image.
 * @GWY_RESPONSE_SYNTH_INIT_Z: Set value scale to match the value scale of the current image.
 *
 * Dialog responses for buttons created by gwy_synth_append_dimensions_to_param_table().
 *
 * Since: 2.59
 **/

/**
 * GwySynthDimsParam:
 * @GWY_DIMS_PARAM_XRES: Horizontal pixel resolution.
 * @GWY_DIMS_PARAM_YRES: Vertical pixel resolution.
 * @GWY_DIMS_PARAM_SQUARE_IMAGE: Whether the image is square (pixelwise).
 * @GWY_DIMS_PARAM_XREAL: Horizontal physical dimension, in base SI units.
 * @GWY_DIMS_PARAM_YREAL: Vertical physical dimension, in base SI units.
 * @GWY_DIMS_PARAM_SQUARE_PIXELS: Whether pixels are physically square.
 * @GWY_DIMS_PARAM_XYUNIT: Unit of lateral dimensions.
 * @GWY_DIMS_PARAM_ZUNIT: Unit of values.
 * @GWY_DIMS_PARAM_REPLACE: Whether the simulation result should replace the current image.
 * @GWY_DIMS_PARAM_INITIALIZE: Whether the simulation should start from the current image.
 * @GWY_DIMS_BUTTON_TAKE: Button Like Current Image.
 * @GWY_DIMS_HEADER_PIXEL: Header for the pixel dimensions section.
 * @GWY_DIMS_HEADER_PHYSICAL: Header for the physical dimensions section.
 * @GWY_DIMS_HEADER_UNITS: Header for the unit section.
 * @GWY_DIMS_HEADER_CURRENT_IMAGE: Header for the Current Image section.
 *
 * Ids of parameters created by gwy_synth_define_dimensions_params().
 *
 * The enum also includes a few GUI elements to satisfy the id uniqueness required by #GwyParamTable.
 *
 * Since: 2.59
 **/

/**
 * GwySynthDimsFlags:
 * @GWY_SYNTH_FIXED_XYUNIT: The lateral unit is fixed and cannot be set by the user.
 * @GWY_SYNTH_FIXED_ZUNIT: The value unit is fixed and cannot be set by the user.
 * @GWY_SYNTH_FIXED_UNITS: No units can be set by the uset.
 * @GWY_SYNTH_NO_INITIALIZE: The option to start from the current image is not available. (Since 2.62)
 *
 * Possible flags passed to gwy_synth_append_dimensions_to_param_table().
 *
 * Since: 2.59
 **/

/**
 * GwySynthUpdateType:
 * @GWY_SYNTH_UPDATE_CANCELLED: The computation was cancelled by the user.
 * @GWY_SYNTH_UPDATE_NOTHING: Nothing has changed; proceed with computation.
 * @GWY_SYNTH_UPDATE_DO_PREVIEW: Render a preview of the current computation state.
 *
 * Possible return values from gwy_synth_update_progress().
 *
 * Since: 2.59
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
