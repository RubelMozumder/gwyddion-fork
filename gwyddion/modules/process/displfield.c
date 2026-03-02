/*
 *  $Id: displfield.c 24796 2022-04-28 15:20:59Z yeti-dn $
 *  Copyright (C) 2019-2021 David Necas (Yeti).
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
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/arithmetic.h>
#include <libprocess/correct.h>
#include <libprocess/filters.h>
#include <libprocess/stats.h>
#include <libprocess/synth.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    DISPL_FIELD_METHOD_1D_GAUSSIAN = 0,
    DISPL_FIELD_METHOD_2D_GAUSSIAN = 1,
    DISPL_FIELD_METHOD_1D_TEAR     = 2,
    DISPL_FIELD_METHOD_1D_IMAGE    = 3,
    DISPL_FIELD_METHOD_2D_IMAGES   = 4,
    DISPL_FIELD_NMODES
} DisplFieldMethod;

enum {
    PARAM_DENSITY,
    PARAM_EXTERIOR,
    PARAM_INTERP,
    PARAM_METHOD,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_SIGMA,
    PARAM_TAU,
    PARAM_UPDATE,
    PARAM_X_DISPLACEMENT,
    PARAM_Y_DISPLACEMENT,
};

typedef struct {
    gint xres;
    gint yres;
    const gdouble *xdata;
    const gdouble *ydata;
} DisplacementData;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register      (void);
static GwyParamDef*     define_module_params (void);
static void             displ_field          (GwyContainer *data,
                                              GwyRunType runtype);
static void             execute              (ModuleArgs *args);
static GwyDialogOutcome run_gui              (ModuleArgs *args,
                                              GwyContainer *data,
                                              gint id);
static void             param_changed        (ModuleGUI *gui,
                                              gint id);
static void             preview              (gpointer user_data);
static gboolean         displ_field_filter   (GwyContainer *data,
                                              gint id,
                                              gpointer user_data);
static GwyDataField*    make_displacement_map(guint xres,
                                              guint yres,
                                              gdouble sigma,
                                              gdouble tau,
                                              GRand *rng);
static GwyDataField*    make_tear_map        (guint xres,
                                              guint yres,
                                              gdouble sigma,
                                              gdouble density,
                                              gdouble length,
                                              GRand *rng);
static void             sanitise_params      (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Distorts image or individual scan lines in plane using a displacement field."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2019",
};

GWY_MODULE_QUERY2(module_info, displfield)

static gboolean
module_register(void)
{
    gwy_process_func_register("displfield",
                              (GwyProcessFunc)&displ_field,
                              N_("/_Distortion/Displacement _Field..."),
                              GWY_STOCK_DISPLACEMENT_FIELD,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              _("Deform image or scan lines in plane"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum methods[] = {
        { N_("Gaussian (scan lines)"),      DISPL_FIELD_METHOD_1D_GAUSSIAN, },
        { N_("Gaussian (two-dimensional)"), DISPL_FIELD_METHOD_2D_GAUSSIAN, },
        { N_("Tear scan lines"),            DISPL_FIELD_METHOD_1D_TEAR,     },
        { N_("Image (scan lines)"),         DISPL_FIELD_METHOD_1D_IMAGE,    },
        { N_("Images (two-dimensional)"),   DISPL_FIELD_METHOD_2D_IMAGES,   },
    };
    static const GwyEnum exteriors[] = {
        { N_("exterior|Border"),   GWY_EXTERIOR_BORDER_EXTEND, },
        { N_("exterior|Mirror"),   GWY_EXTERIOR_MIRROR_EXTEND, },
        { N_("exterior|Periodic"), GWY_EXTERIOR_PERIODIC,      },
        { N_("exterior|Laplace"),  GWY_EXTERIOR_LAPLACE,       },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_double(paramdef, PARAM_DENSITY, "density", _("Densi_ty"), 0.0001, 0.25, 0.02);
    gwy_param_def_add_gwyenum(paramdef, PARAM_EXTERIOR, "exterior", _("_Exterior type"),
                              exteriors, G_N_ELEMENTS(exteriors), GWY_EXTERIOR_BORDER_EXTEND);
    gwy_param_def_add_enum(paramdef, PARAM_INTERP, "interp", NULL,
                           GWY_TYPE_INTERPOLATION_TYPE, GWY_INTERPOLATION_LINEAR);
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "method", _("_Method"),
                              methods, G_N_ELEMENTS(methods), DISPL_FIELD_METHOD_2D_GAUSSIAN);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_double(paramdef, PARAM_SIGMA, "sigma", _("_Amplitude"), 0.0, 100.0, 10.0);
    gwy_param_def_add_double(paramdef, PARAM_TAU, "tau", _("_Lateral scale"), 0.1, 1000.0, 50.0);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_image_id(paramdef, PARAM_X_DISPLACEMENT, "x_displacement", _("_X displacement"));
    gwy_param_def_add_image_id(paramdef, PARAM_Y_DISPLACEMENT, "y_displacement", _("_Y displacement"));
    return paramdef;
}

static void
displ_field(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    args.result = gwy_data_field_duplicate(args.field);
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE_TYPE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    gwy_app_set_data_field_title(data, newid, _("Distorted"));
    gwy_app_channel_log_add_proc(data, id, newid);

end:
    g_object_unref(args.params);
    g_object_unref(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    ModuleGUI gui;
    GtkWidget *hbox, *dataview;
    GwyDialog *dialog;
    GwyParamTable *table;
    GwyDialogOutcome outcome;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Displacement Field"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_header(table, -1, _("Displacement Field"));
    gwy_param_table_append_combo(table, PARAM_METHOD);
    gwy_param_table_append_image_id(table, PARAM_X_DISPLACEMENT);
    gwy_param_table_data_id_set_filter(table, PARAM_X_DISPLACEMENT, displ_field_filter, args->field, NULL);
    gwy_param_table_append_image_id(table, PARAM_Y_DISPLACEMENT);
    gwy_param_table_data_id_set_filter(table, PARAM_Y_DISPLACEMENT, displ_field_filter, args->field, NULL);

    gwy_param_table_append_slider(table, PARAM_SIGMA);
    gwy_param_table_slider_add_alt(table, PARAM_SIGMA);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_SIGMA, args->field);

    gwy_param_table_append_slider(table, PARAM_TAU);
    gwy_param_table_slider_set_mapping(table, PARAM_TAU, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_TAU);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_TAU, args->field);

    gwy_param_table_append_slider(table, PARAM_DENSITY);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_combo(table, PARAM_EXTERIOR);
    gwy_param_table_append_combo(table, PARAM_INTERP);
    gwy_param_table_append_seed(table, PARAM_SEED);
    gwy_param_table_append_checkbox(table, PARAM_RANDOMIZE);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);
    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;
    gboolean has_any = !gwy_params_data_id_is_none(params, PARAM_X_DISPLACEMENT);
    DisplFieldMethod method = gwy_params_get_enum(params, PARAM_METHOD);

    if (id < 0 || id == PARAM_METHOD) {
        gboolean needs_xdef = (method == DISPL_FIELD_METHOD_1D_IMAGE || method == DISPL_FIELD_METHOD_2D_IMAGES);
        gboolean needs_ydef = (method == DISPL_FIELD_METHOD_2D_IMAGES);
        gboolean has_density = (method == DISPL_FIELD_METHOD_1D_TEAR);
        gboolean has_gaussian = (method == DISPL_FIELD_METHOD_1D_TEAR
                                 || method == DISPL_FIELD_METHOD_2D_GAUSSIAN
                                 || method == DISPL_FIELD_METHOD_1D_GAUSSIAN);
        gwy_param_table_set_sensitive(table, PARAM_X_DISPLACEMENT, needs_xdef && has_any);
        gwy_param_table_set_sensitive(table, PARAM_Y_DISPLACEMENT, needs_ydef && has_any);
        gwy_param_table_set_sensitive(table, PARAM_DENSITY, has_density);
        gwy_param_table_set_sensitive(table, PARAM_SIGMA, has_gaussian);
        gwy_param_table_set_sensitive(table, PARAM_TAU, has_gaussian);
        gwy_param_table_set_sensitive(table, PARAM_SEED, has_gaussian);
        gwy_param_table_set_sensitive(table, PARAM_RANDOMIZE, has_gaussian);
    }
    if (id != PARAM_UPDATE && id != PARAM_RANDOMIZE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static gboolean
displ_field_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *other_image, *field = (GwyDataField*)user_data;
    GwySIUnit *fxyunit, *dzunit;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &other_image))
        return FALSE;
    if (other_image == field)
        return FALSE;
    if (gwy_data_field_check_compatibility(other_image, field,
                                           GWY_DATA_COMPATIBILITY_RES
                                           | GWY_DATA_COMPATIBILITY_REAL
                                           | GWY_DATA_COMPATIBILITY_LATERAL))
        return FALSE;

    /* Values of the distortion field are lateral offsets in the image. */
    fxyunit = gwy_data_field_get_si_unit_xy(field);
    dzunit = gwy_data_field_get_si_unit_z(other_image);

    return gwy_si_unit_equal(fxyunit, dzunit);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args);
    gwy_data_field_data_changed(gui->args->result);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
distort_func_2d(gdouble x, gdouble y, gdouble *px, gdouble *py, gpointer user_data)
{
    const DisplacementData *dd = (const DisplacementData*)user_data;
    gint i, j, k;

    j = (gint)floor(x);
    j = CLAMP(j, 0, dd->xres-1);
    i = (gint)floor(y);
    i = CLAMP(i, 0, dd->yres-1);
    k = i*dd->xres + j;
    *px = x - dd->xdata[k];
    *py = y - dd->ydata[k];
}

static GwyDataField*
create_displ_field(GwyParams *params, gint id)
{
    GwyDataField *displfield = gwy_params_get_image(params, id);
    return displfield ? gwy_data_field_duplicate(displfield) : NULL;
}

static void
execute(ModuleArgs *args)
{
    GwyDataField *result = args->result, *field = args->field, *xdisplfield = NULL, *ydisplfield = NULL;
    GwyParams *params = args->params;
    DisplFieldMethod method = gwy_params_get_enum(params, PARAM_METHOD);
    gdouble sigma = gwy_params_get_double(params, PARAM_SIGMA);
    gdouble tau = gwy_params_get_double(params, PARAM_TAU);
    gdouble density = gwy_params_get_double(params, PARAM_DENSITY);
    GwyInterpolationType interp = gwy_params_get_enum(params, PARAM_INTERP);
    GwyExteriorType exterior = gwy_params_get_enum(params, PARAM_EXTERIOR);
    DisplacementData dd;
    GRand *rng;
    gint xres, yres;

    gwy_clear(&dd, 1);
    xres = dd.xres = gwy_data_field_get_xres(field);
    yres = dd.yres = gwy_data_field_get_yres(field);
    rng = g_rand_new_with_seed(gwy_params_get_int(params, PARAM_SEED));

    if (method == DISPL_FIELD_METHOD_2D_GAUSSIAN) {
        xdisplfield = make_displacement_map(xres, yres, sigma, tau, rng);
        ydisplfield = make_displacement_map(xres, yres, sigma, tau, rng);
    }
    else if (method == DISPL_FIELD_METHOD_1D_GAUSSIAN)
        xdisplfield = make_displacement_map(xres, yres, sigma, tau, rng);
    else if (method == DISPL_FIELD_METHOD_1D_TEAR)
        xdisplfield = make_tear_map(xres, yres, sigma, density, tau, rng);
    else if (method == DISPL_FIELD_METHOD_1D_IMAGE) {
        if ((xdisplfield = create_displ_field(params, PARAM_X_DISPLACEMENT)))
            gwy_data_field_multiply(xdisplfield, 1.0/gwy_data_field_get_dx(field));
    }
    else if (method == DISPL_FIELD_METHOD_2D_IMAGES) {
        if ((xdisplfield = create_displ_field(params, PARAM_X_DISPLACEMENT))
            && (ydisplfield = create_displ_field(params, PARAM_Y_DISPLACEMENT))) {
            gwy_data_field_multiply(xdisplfield, 1.0/gwy_data_field_get_dx(field));
            gwy_data_field_multiply(ydisplfield, 1.0/gwy_data_field_get_dy(field));
        }
        else {
            GWY_OBJECT_UNREF(xdisplfield);
            GWY_OBJECT_UNREF(ydisplfield);
        }
    }

    if (xdisplfield) {
        if (!ydisplfield)
            ydisplfield = gwy_data_field_new_alike(xdisplfield, TRUE);

        dd.xdata = gwy_data_field_get_data_const(xdisplfield);
        dd.ydata = gwy_data_field_get_data_const(ydisplfield);
        gwy_data_field_distort(field, result, &distort_func_2d, &dd, interp, exterior, 0.0);
    }
    else
        gwy_data_field_copy(field, result, FALSE);

    GWY_OBJECT_UNREF(xdisplfield);
    GWY_OBJECT_UNREF(ydisplfield);
    g_rand_free(rng);
}

static gboolean
collides_with_another_tear(const gdouble *m,
                           gint xres, gint yres,
                           gint col, gint row, gint len)
{
    gint ifrom = MAX(row-1, 0), ito = MIN(row+2+1, yres);
    gint jfrom = MAX(col-len/2-1, 0), jto = MIN(col+len/2+2, xres);
    gint i, j;

    for (i = ifrom; i < ito; i++) {
        const gdouble *mrow = m + i*xres;
        for (j = jfrom; j < jto; j++) {
            if (mrow[j] != 0.0)
                return TRUE;
        }
    }

    return FALSE;
}

static void
fill_tear(gdouble *d, gdouble *m,
          gint xres, gint yres,
          gint col, gint row, gint len,
          gdouble value)
{
    gint jfrom = MAX(col-len/2, 0), jto = MIN(col+len/2+1, xres);
    gint j;

    if (row >= 0) {
        gdouble *drow = d + row*xres;
        gdouble *mrow = m + row*xres;
        for (j = jfrom; j < jto; j++) {
            gdouble t = (j - col)/(gdouble)(len/2), tp = 1.0 + t, tm = 1.0 - t;
            drow[j] = value*tm*tm*tp*tp;
            mrow[j] = 1.0;
        }
    }
    if (row < yres-1) {
        gdouble *drow = d + (row + 1)*xres;
        gdouble *mrow = m + (row + 1)*xres;
        for (j = jfrom; j < jto; j++) {
            gdouble t = (j - col)/(gdouble)(len/2), tp = 1.0 + t, tm = 1.0 - t;
            drow[j] = -value*tm*tm*tp*tp;
            mrow[j] = 1.0;
        }
    }
}

static GwyDataField*
make_displacement_map(guint xres, guint yres,
                      gdouble sigma, gdouble tau,
                      GRand *rng)
{
    GwyDataField *field = gwy_data_field_new(xres, yres, 1.0, 1.0, TRUE);
    gwy_data_field_synth_gaussian_displacement(field, sigma, tau, rng);
    return field;
}

static GwyDataField*
make_tear_map(guint xres, guint yres,
              gdouble sigma, gdouble density, gdouble length,
              GRand *rng)
{
    GwyDataField *field, *mask;
    gdouble *d, *m;
    guint n, k;

    field = gwy_data_field_new(xres, yres, 1.0, 1.0, TRUE);
    n = GWY_ROUND(0.5*xres*yres*density/length);
    if (!n)
        return field;

    mask = gwy_data_field_new(xres, yres, 1.0, 1.0, TRUE);
    d = gwy_data_field_get_data(field);
    m = gwy_data_field_get_data(mask);

    for (k = 0; k < n; k++) {
        gint col, row, len, count = 0;
        gdouble value;

        do {
            /* The tear is a two-line structure, so start from -1 for symmetry as its second line can also go one line
             * beyond. */
            row = g_rand_int_range(rng, -1, yres);
            col = g_rand_int_range(rng, -length, xres + length);
            len = GWY_ROUND(length + (g_rand_double(rng) + g_rand_double(rng) + g_rand_double(rng) - 1.5)*length/5.0);
            count++;
        } while (count < 100 && (len < 2 || collides_with_another_tear(m, xres, yres, col, row, len)));

        if (count == 100) {
            //g_printerr("Cannot place tear #%u.\n", k+1);
            break;
        }

        value = sigma*(g_rand_double(rng) - 0.5);
        fill_tear(d, m, xres, yres, col, row, len, value);
    }

    gwy_data_field_laplace_solve(field, mask, 0, 0.5);
    g_object_unref(mask);

    return field;
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    DisplFieldMethod method = gwy_params_get_enum(params, PARAM_METHOD);
    GwyAppDataId xdisplid = gwy_params_get_data_id(params, PARAM_X_DISPLACEMENT);
    GwyAppDataId ydisplid = gwy_params_get_data_id(params, PARAM_Y_DISPLACEMENT);
    gboolean x_is_none = gwy_params_data_id_is_none(params, PARAM_X_DISPLACEMENT);
    gboolean y_is_none = gwy_params_data_id_is_none(params, PARAM_Y_DISPLACEMENT);

    if (method == DISPL_FIELD_METHOD_2D_IMAGES
        && (x_is_none
            || y_is_none
            || !displ_field_filter(gwy_app_data_browser_get(xdisplid.datano), xdisplid.id, args->field)
            || !displ_field_filter(gwy_app_data_browser_get(ydisplid.datano), ydisplid.id, args->field)))
        gwy_params_reset(params, PARAM_METHOD);

    if (method == DISPL_FIELD_METHOD_1D_IMAGE
        && (x_is_none || !displ_field_filter(gwy_app_data_browser_get(xdisplid.datano), xdisplid.id, args->field)))
        gwy_params_reset(params, PARAM_METHOD);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
