/*
 *  $Id: indent_analyze.c 24775 2022-04-26 15:03:27Z yeti-dn $
 *  Copyright (C) 2006-2022 Lukas Chvatal, David Necas (Yeti), Petr Klapetek.
 *  E-mail: chvatal@physics.muni.cz, yeti@gwyddion.net, klapetek@gwyddion.net.
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
#include <libgwymodule/gwymodule.h>
#include <libprocess/datafield.h>
#include <libprocess/gwyprocess.h>
#include <libgwydgets/gwydgets.h>
#include <app/settings.h>
#include <app/app.h>
#include <app/gwyapp.h>
#include <gtk/gtk.h>
#include "preview.h"

#define RUN_MODES GWY_RUN_INTERACTIVE

enum {
    MAX_PYRAMID_SIDES = 4
};

typedef enum {
    RESULT_NOTHING = 0,
    RESULT_EXTERIOR,
    RESULT_ABOVE,             /* Auxiliary, not shown. */
    RESULT_BELOW,             /* Auxiliary, not shown. */
    RESULT_PLANE,
    RESULT_IMPRINT,
    RESULT_IMPRINT_FACES,
    RESULT_CONTACT_AREA,
    RESULT_PILEUP,
    RESULT_INNER_PILEUP,
    RESULT_OUTER_PILEUP,
    RESULT_FACES_BORDER,
    RESULT_NTYPES
} IndentDisplayType;

typedef enum {
    INDENTER_SPHERE   = 0,
    INDENTER_PYRAMID3 = 3,
    INDENTER_PYRAMID4 = 4,
} GwyIndenterType;

enum {
    PARAM_DO_LEVEL,
    PARAM_EXTERIOR,
    PARAM_PLANE_TOL,
    PARAM_PHI_TOL,
    PARAM_INDENTER,
    PARAM_DISPLAY,
    PARAM_SET_MASK,
    PARAM_MASK_COLOR,
    PARAM_REPORT_STYLE,

    WIDGET_RESULTS,
};

/* p = projected, d = developed (i.e. surface) */
typedef struct {
    gdouble x0;
    gdouble y0;
    gdouble zmin;
    gdouble zmax;
    gdouble Vimp;
    gdouble Vpileup;
    gdouble Acontact;
    gdouble Asurf_imp;
    gdouble Aproj_imp;
    gdouble Asurf_pileup;
    gdouble Aproj_pileup;
    gdouble Asurf_in;
    gdouble Aproj_in;
    gdouble Asurf_out;
    gdouble Aproj_out;
    /* Auxiliary. */
    gdouble rms_base;
    gdouble phi;
} ImprintParameters;

typedef struct {
    GwyXY apex;
    GwyXY side[MAX_PYRAMID_SIDES];
} PyramidParameters;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *adjusted;
    GwyDataField *xder;
    GwyDataField *yder;
    GwyDataField *result[RESULT_NTYPES];
    GwySelection *selection;
    ImprintParameters imp;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_param;
    GwyParamTable *table_results;
    GwyContainer *data;
    GwyResults *results;
} ModuleGUI;

static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static void             indent_analyze          (GwyContainer *data,
                                                 GwyRunType runtype);
static void             execute                 (ModuleArgs *args);
static GwyDialogOutcome run_gui                 (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static GwyResults*      create_results          (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static void             param_changed           (ModuleGUI *gui,
                                                 gint id);
static void             preview                 (gpointer user_data);
static void             level_using_exterior    (GwyDataField *field,
                                                 GwyDataField *mask,
                                                 gdouble wfrac,
                                                 gboolean do_level);
static void             compute_slopes          (GwyDataField *field,
                                                 gint kernel_size,
                                                 GwyDataField *xder,
                                                 GwyDataField *yder,
                                                 GwyDataField *atanfield);
static gdouble          find_prevalent_phi      (GwyDataField *field,
                                                 GwyDataField *exclmask,
                                                 GwyDataField *xder,
                                                 GwyDataField *yder,
                                                 GwyDataField *atanfield);
static void             find_imprint_side_centre(GwyDataField *field,
                                                 gdouble search_to_z,
                                                 gdouble phi,
                                                 gdouble *xcentre,
                                                 gdouble *ycentre);
static void             calc_mean_normal        (GwyDataField *xder,
                                                 GwyDataField *yder,
                                                 gdouble x,
                                                 gdouble y,
                                                 gdouble r,
                                                 GwyXYZ *v);
static void             mark_facet              (GwyDataField *xder,
                                                 GwyDataField *yder,
                                                 GwyDataField *mask,
                                                 gdouble theta0,
                                                 gdouble phi0,
                                                 gdouble tolerance);
static void             mark_pileup             (GwyDataField *above,
                                                 GwyDataField *imprint,
                                                 GwyDataField *pileup);
static void             calc_surface_and_volume (GwyDataField *field,
                                                 GwyDataField *mask,
                                                 gdouble *Aproj,
                                                 gdouble *Asurf,
                                                 gdouble *V);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Analyses nanoindentation structure (volumes, surfaces, ...)."),
    "Lukáš Chvátal <chvatal@physics.muni.cz> & Yeti <yeti@physics.muni.cz>",
    "1.0",
    "Lukáš Chvátal",
    "2005",
};

GWY_MODULE_QUERY2(module_info, indent_analyze)

static gboolean
module_register(void)
{
    gwy_process_func_register("indent_analyze",
                              (GwyProcessFunc)&indent_analyze,
                              N_("/SPM M_odes/_Force and Indentation/_Analyze Imprint..."),
                              GWY_STOCK_TIP_INDENT_ANALYZE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              _("Analyze indentation imprint"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum indenters[] = {
        { N_("Sphere"),              INDENTER_SPHERE,   },
        { N_("Pyramid (3-sided)"),   INDENTER_PYRAMID3, },
        { N_("Pyramid (rectangle)"), INDENTER_PYRAMID4, },
    };
    static const GwyEnum displays[] = {
        { N_("Nothing"),       RESULT_NOTHING,       },
        { N_("Exterior"),      RESULT_EXTERIOR,      },
        { N_("Plane"),         RESULT_PLANE,         },
        { N_("Imprint"),       RESULT_IMPRINT,       },
        { N_("Imprint faces"), RESULT_IMPRINT_FACES, },
        { N_("Contact area"),  RESULT_CONTACT_AREA,  },
        { N_("Pile-up"),       RESULT_PILEUP,        },
        { N_("Inner pile-up"), RESULT_INNER_PILEUP,  },
        { N_("Outer pile-up"), RESULT_OUTER_PILEUP,  },
        { N_("Faces border"),  RESULT_FACES_BORDER,  },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_boolean(paramdef, PARAM_DO_LEVEL, "do_level", _("Level using imprint exterior"), TRUE);
    gwy_param_def_add_double(paramdef, PARAM_EXTERIOR, "border", _("Exterior width"), 1.0, 40.0, 5.0);
    gwy_param_def_add_double(paramdef, PARAM_PLANE_TOL, "plane_tol", _("Ref. plane _tolerance"), 0.0, 8.0, 2.0);
    gwy_param_def_add_double(paramdef, PARAM_PHI_TOL, "phi_tol", _("_Angle tolerance"), 0.0, G_PI, 8.0*G_PI/180.0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_INDENTER, "indentor", _("_Indenter shape"),
                              indenters, G_N_ELEMENTS(indenters), INDENTER_PYRAMID3);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISPLAY, "display", gwy_sgettext("verb|_Display"),
                              displays, G_N_ELEMENTS(displays), RESULT_NOTHING);
    gwy_param_def_add_boolean(paramdef, PARAM_SET_MASK, "set_mask", _("Create _mask"), TRUE);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    gwy_param_def_add_report_type(paramdef, PARAM_REPORT_STYLE, "report_style", NULL,
                                  GWY_RESULTS_EXPORT_PARAMETERS, GWY_RESULTS_REPORT_COLON);
    return paramdef;
}

static void
indent_analyze(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    IndentDisplayType display;
    gboolean set_mask;
    ModuleArgs args;
    GQuark mquark;
    gint id, i;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     0);
    g_return_if_fail(args.field && mquark);

    if (!gwy_require_image_same_units(args.field, data, id, _("Analyze imprint")))
        return;

    args.adjusted = gwy_data_field_duplicate(args.field);
    for (i = 1; i < RESULT_NTYPES; i++) {
        args.result[i] = gwy_data_field_new_alike(args.field, TRUE);
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result[i]), NULL);
    }
    args.xder = gwy_data_field_new_alike(args.field, TRUE);
    args.yder = gwy_data_field_new_alike(args.field, TRUE);
    args.params = gwy_params_new_from_settings(define_module_params());

    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    set_mask = gwy_params_get_boolean(args.params, PARAM_SET_MASK);
    display = gwy_params_get_enum(args.params, PARAM_DISPLAY);
    if (set_mask && display != RESULT_NOTHING) {
        if (outcome != GWY_DIALOG_HAVE_RESULT)
            execute(&args);
        gwy_app_undo_qcheckpointv(data, 1, &mquark);
        if (gwy_data_field_get_max(args.result[display]) > 0.0)
            gwy_container_set_object(data, mquark, args.result[display]);
        else
            gwy_container_remove(data, mquark);
        gwy_app_channel_log_add_proc(data, id, id);
    }

end:
    for (i = 1; i < RESULT_NTYPES; i++)
        g_object_unref(args.result[i]);
    g_object_unref(args.params);
    g_object_unref(args.adjusted);
    g_object_unref(args.xder);
    g_object_unref(args.yder);
    /* TODO: We can set the selection on the image. */
    GWY_OBJECT_UNREF(args.selection);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GtkWidget *hbox, *dataview;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gui.results = create_results(args, data, id);

    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->adjusted);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Analyze Imprint"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    args->selection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(dataview), 0, "Point", 5, FALSE);
    g_object_ref(args->selection);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table_param = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Leveling"));
    gwy_param_table_append_checkbox(table, PARAM_DO_LEVEL);
    gwy_param_table_append_slider(table, PARAM_EXTERIOR);
    gwy_param_table_set_unitstr(table, PARAM_EXTERIOR, "%");

    gwy_param_table_append_header(table, -1, _("Marking"));
    gwy_param_table_append_combo(table, PARAM_INDENTER);
    gwy_param_table_append_slider(table, PARAM_PLANE_TOL);
    gwy_param_table_set_unitstr(table, PARAM_PLANE_TOL, _("RMS"));
    gwy_param_table_append_slider(table, PARAM_PHI_TOL);
    gwy_param_table_slider_set_factor(table, PARAM_PHI_TOL, 180.0/G_PI);
    gwy_param_table_set_unitstr(table, PARAM_PHI_TOL, _("deg"));
    gwy_param_table_append_radio(table, PARAM_DISPLAY);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, data, id);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    table = gui.table_results = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Result"));
    gwy_param_table_append_results(table, WIDGET_RESULTS, gui.results,
                                   "x0", "y0", "zmin", "zmax",
                                   "Vimp", "Asurf_imp", "Aproj_imp", "Acontact",
                                   "Vpileup", "Asurf_pileup", "Aproj_pileup",
                                   "Asurf_in", "Aproj_in", "Asurf_out", "Aproj_out",
                                   NULL);
    gwy_param_table_append_report(table, PARAM_REPORT_STYLE);
    gwy_param_table_report_set_results(table, PARAM_REPORT_STYLE, gui.results);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_header(table, -1, _("Output"));
    gwy_param_table_append_checkbox(table, PARAM_SET_MASK);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(gui.table_param, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_results, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    g_object_unref(gui.results);

    return outcome;
}

static GwyResults*
create_results(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyResults *results = gwy_results_new();

    gwy_results_add_header(results, N_("Indentation"));
    gwy_results_add_value_str(results, "file", N_("File"));
    gwy_results_add_value_str(results, "image", N_("Image"));
    gwy_results_add_separator(results);

    gwy_results_add_value_x(results, "x0", N_("Imprint center x"));
    gwy_results_add_value_x(results, "y0", N_("Imprint center y"));
    gwy_results_add_value_z(results, "zmin", N_("Center value"));
    gwy_results_add_value_z(results, "zmax", N_("Maximum"));
    /* The units must be all the same anyway... */
    gwy_results_add_value(results, "Asurf_imp", N_("Imprint surface area"), "power-x", 2, NULL);
    gwy_results_add_value(results, "Aproj_imp", N_("Imprint projected area"), "power-x", 2, NULL);
    gwy_results_add_value(results, "Acontact", N_("Contact area"), "power-x", 2, NULL);
    gwy_results_add_value(results, "Vimp", N_("Imprint volume"), "power-x", 2, "power-z", 1, NULL);
    gwy_results_add_value(results, "Vpileup", N_("Pile-up volume"), "power-x", 2, "power-z", 1, NULL);
    gwy_results_add_value(results, "Asurf_pileup", N_("Pile-up surface area"), "power-x", 2, NULL);
    gwy_results_add_value(results, "Aproj_pileup", N_("Pile-up projected area"), "power-x", 2, NULL);
    gwy_results_add_value(results, "Asurf_in", N_("Inner pile-up surface area"), "power-x", 2, NULL);
    gwy_results_add_value(results, "Aproj_in", N_("Inner pile-up projected area"), "power-x", 2, NULL);
    gwy_results_add_value(results, "Asurf_out", N_("Outer pile-up surface area"), "power-x", 2, NULL);
    gwy_results_add_value(results, "Aproj_out", N_("Outer pile-up projected area"), "power-x", 2, NULL);

    gwy_results_bind_formats(results, "x0", "y0", NULL);
    gwy_results_bind_formats(results, "zmin", "zmax", NULL);
    gwy_results_bind_formats(results,
                             "Asurf_imp", "Aproj_imp", "Acontact", "Asurf_pileup", "Aproj_pileup",
                             "Asurf_in", "Aproj_in", "Asurf_out", "Aproj_out",
                             NULL);
    gwy_results_bind_formats(results, "Vimp", "Vpileup", NULL);

    gwy_results_set_unit(results, "x", gwy_data_field_get_si_unit_xy(args->field));
    gwy_results_set_unit(results, "z", gwy_data_field_get_si_unit_z(args->field));
    gwy_results_fill_filename(results, "file", data);
    gwy_results_fill_channel(results, "image", data, id);

    return results;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (id < 0 || id == PARAM_DISPLAY) {
        IndentDisplayType display = gwy_params_get_enum(params, PARAM_DISPLAY);

        if (display == RESULT_NOTHING)
            gwy_container_remove(gui->data, gwy_app_get_mask_key_for_id(0));
        else
            gwy_container_set_object(gui->data, gwy_app_get_mask_key_for_id(0), args->result[display]);
    }
    if (id < 0 || id == PARAM_INDENTER) {
        GwyIndenterType indenter = gwy_params_get_enum(params, PARAM_INDENTER);
        gint nsides = indenter;

        gwy_param_table_set_sensitive(gui->table_param, PARAM_PHI_TOL, !!nsides);
        if (nsides)
            gwy_param_table_slider_restrict_range(gui->table_param, PARAM_PHI_TOL, 0.0, G_PI/nsides);
    }

    if (id != PARAM_MASK_COLOR && id != PARAM_REPORT_STYLE && id != PARAM_DISPLAY)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    ImprintParameters *imp = &args->imp;
    gint i;

    execute(args);
    gwy_data_field_data_changed(args->adjusted);
    for (i = 1; i < RESULT_NTYPES; i++)
        gwy_data_field_data_changed(args->result[i]);
    gwy_results_fill_values(gui->results,
                            "x0", imp->x0, "y0", imp->y0, "zmin", imp->zmin, "zmax", imp->zmax,
                            "Aproj_imp", imp->Aproj_imp, "Asurf_imp", imp->Asurf_imp, "Acontact", imp->Acontact,
                            "Vimp", imp->Vimp, "Vpileup", imp->Vpileup,
                            "Asurf_pileup", imp->Asurf_pileup, "Aproj_pileup", imp->Aproj_pileup,
                            "Aproj_in", imp->Aproj_in, "Asurf_in", imp->Asurf_in,
                            "Aproj_out", imp->Aproj_out, "Asurf_out", imp->Asurf_out,
                            NULL);
    gwy_param_table_results_fill(gui->table_results, WIDGET_RESULTS);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    ImprintParameters *imp = &args->imp;
    GwyDataField *adjusted = args->adjusted, *xder = args->xder, *yder = args->yder, *buf, *atanfield, *kernel;
    GwyDataField *imprint = args->result[RESULT_IMPRINT];
    GwyDataField *above = args->result[RESULT_ABOVE];
    GwyDataField *below = args->result[RESULT_BELOW];
    GwyDataField *impfaces = args->result[RESULT_IMPRINT_FACES];
    GwyDataField *facesborder = args->result[RESULT_FACES_BORDER];
    GwyDataField *pileup = args->result[RESULT_PILEUP];
    gboolean do_level = gwy_params_get_boolean(params, PARAM_DO_LEVEL);
    gdouble exterior = gwy_params_get_double(params, PARAM_EXTERIOR);
    gdouble plane_tol = gwy_params_get_double(params, PARAM_PLANE_TOL);
    gdouble phi_tol = gwy_params_get_double(params, PARAM_PHI_TOL);
    GwyIndenterType indenter = gwy_params_get_enum(params, PARAM_INDENTER);
    gint xres = gwy_data_field_get_xres(args->field), yres = gwy_data_field_get_yres(args->field);
    gint i, ksize, nsides = indenter;
    gdouble points[2*5];
    gdouble search_to_z, dx, dy;

    /* Level (if requested) using exterior and find the base plane and its rms. */
    gwy_data_field_copy(args->field, adjusted, FALSE);
    dx = gwy_data_field_get_dx(adjusted);
    dy = gwy_data_field_get_dy(adjusted);
    level_using_exterior(adjusted, args->result[RESULT_EXTERIOR], exterior/100.0, do_level);
    if (!gwy_data_field_get_local_minima_list(adjusted, &imp->x0, &imp->y0, &imp->zmin, 1, 0, G_MAXDOUBLE, TRUE)) {
        g_printerr("cannot find any minimum! fixme to handle this!\n");
        return;
    }
    imp->zmax = gwy_data_field_get_max(adjusted);
    imp->rms_base = gwy_data_field_area_get_rms_mask(adjusted, args->result[RESULT_EXTERIOR], GWY_MASK_INCLUDE,
                                                     0, 0, xres, yres);
    plane_tol *= imp->rms_base;

    /* Calculate slopes and find the imprint face. */
    /* use FACES_BORDER and OUTER_PILEUP as temporary buffers */
    atanfield = args->result[RESULT_OUTER_PILEUP];
    buf = args->result[RESULT_FACES_BORDER];
    compute_slopes(adjusted, 5, xder, yder, atanfield);
    gwy_data_field_clear(impfaces);
    imp->phi = find_prevalent_phi(adjusted, impfaces, xder, yder, atanfield);
    search_to_z = imp->zmin - 0.3*imp->zmin;
    gwy_debug("search side from to %g within [%g..0.0]", search_to_z, imp->zmin);
    if (nsides) {
        for (i = 0; i < nsides; i++) {
            gdouble phi = gwy_canonicalize_angle(imp->phi + 2.0*G_PI/nsides*i, TRUE, TRUE);
            gdouble xc = imp->x0, yc = imp->y0, theta0, phi0;
            GwyXYZ v;

            find_imprint_side_centre(adjusted, search_to_z, phi, &xc, &yc);
            points[2*i + 2] = xc*dx;
            points[2*i + 3] = yc*dy;
            calc_mean_normal(xder, yder, xc, yc, 4.5, &v);
            phi0 = atan2(v.y, v.x);
            theta0 = atan(hypot(v.x, v.y));
            gwy_debug("mean normal phi0=%g, theta0=%g", 180.0/G_PI*phi0, 180.0/G_PI*theta0);
            mark_facet(xder, yder, buf, theta0, phi0, phi_tol);
            gwy_data_field_grains_extract_grain(buf, (gint)floor(xc), (gint)floor(yc));
            gwy_data_field_fill_voids(buf, TRUE);
            gwy_data_field_max_of_fields(impfaces, buf, impfaces);
        }
    }

    /* Mark various derived regions. */
    gwy_data_field_copy(adjusted, above, FALSE);
    gwy_data_field_copy(adjusted, below, FALSE);
    gwy_data_field_threshold(above, plane_tol, 0.0, 1.0);
    gwy_data_field_threshold(below, -plane_tol, 1.0, 0.0);
    gwy_data_field_max_of_fields(args->result[RESULT_PLANE], above, below);
    gwy_data_field_threshold(args->result[RESULT_PLANE], 0.5, 1.0, 0.0);

    gwy_data_field_copy(below, imprint, FALSE);
    gwy_data_field_grains_extract_grain(imprint, (gint)floor(imp->x0), (gint)floor(imp->y0));
    gwy_data_field_fill_voids(imprint, TRUE);
    /* FIXME */
    if (!nsides)
        gwy_data_field_copy(imprint, impfaces, FALSE);

    mark_pileup(above, imprint, pileup);
    gwy_data_field_min_of_fields(args->result[RESULT_INNER_PILEUP], pileup, impfaces);
    gwy_data_field_subtract_fields(args->result[RESULT_OUTER_PILEUP], pileup, impfaces);
    gwy_data_field_threshold(args->result[RESULT_INNER_PILEUP], 0.5, 0.0, 1.0);

    ksize = GWY_ROUND(sqrt(xres*yres)/150.0);
    ksize = MAX(ksize, 2);
    kernel = gwy_data_field_new(ksize, ksize, 1.0, 1.0, TRUE);
    gwy_data_field_elliptic_area_fill(kernel, 0, 0, ksize, ksize, 1.0);
    gwy_data_field_copy(impfaces, args->result[RESULT_CONTACT_AREA], FALSE);
    gwy_data_field_area_filter_min_max(args->result[RESULT_CONTACT_AREA], kernel, GWY_MIN_MAX_FILTER_CLOSING,
                                       0, 0, xres, yres);
    g_object_unref(kernel);
    gwy_data_field_fill_voids(args->result[RESULT_CONTACT_AREA], TRUE);

    gwy_data_field_copy(impfaces, facesborder, FALSE);
    gwy_data_field_grains_grow(facesborder, 1.0, GWY_DISTANCE_TRANSFORM_CHESS, FALSE);
    gwy_data_field_threshold(facesborder, 0.5, 0.0, 1.0);
    gwy_data_field_subtract_fields(facesborder, facesborder, impfaces);
    gwy_data_field_threshold(facesborder, 0.5, 0.0, 1.0);

    /* Calculate surfaces and volumes. */
    calc_surface_and_volume(adjusted, impfaces, &imp->Aproj_imp, &imp->Asurf_imp, &imp->Vimp);
    imp->Vimp *= -1.0;
    calc_surface_and_volume(adjusted, pileup, &imp->Aproj_pileup, &imp->Asurf_pileup, &imp->Vpileup);
    calc_surface_and_volume(adjusted, args->result[RESULT_INNER_PILEUP], &imp->Aproj_in, &imp->Asurf_in, NULL);
    calc_surface_and_volume(adjusted, args->result[RESULT_OUTER_PILEUP], &imp->Aproj_out, &imp->Asurf_out, NULL);
    calc_surface_and_volume(adjusted, args->result[RESULT_CONTACT_AREA], &imp->Acontact, NULL, NULL);

    /* Convert pixel locations to real coordinates. */
    imp->x0 *= dx;
    imp->y0 *= dy;

    if (args->selection) {
        points[0] = imp->x0;
        points[1] = imp->y0;
        gwy_selection_set_data(args->selection, 1 + nsides, points);
    }
}

/* Always make average height over the exterior zero.  Plane-level only if requested. */
static void
level_using_exterior(GwyDataField *field, GwyDataField *mask, gdouble wfrac, gboolean do_level)
{
    gint xres = gwy_data_field_get_xres(field), yres = gwy_data_field_get_yres(field);
    gint wx = MAX(GWY_ROUND(xres*wfrac), 1);
    gint wy = MAX(GWY_ROUND(yres*wfrac), 1);
    gdouble a, bx, by;

    gwy_data_field_clear(mask);
    gwy_data_field_area_fill(mask, 0, 0, xres, wy, 1.0);
    gwy_data_field_area_fill(mask, 0, 0, wx, yres, 1.0);
    gwy_data_field_area_fill(mask, 0, yres-wy, xres, wy, 1.0);
    gwy_data_field_area_fill(mask, xres-wx, 0, wx, yres, 1.0);
    if (do_level) {
        gwy_data_field_area_fit_plane_mask(field, mask, GWY_MASK_INCLUDE, 0, 0, xres, yres, &a, &bx, &by);
        gwy_debug("fitted plane bx=%g, by=%g", bx, by);
        gwy_data_field_plane_level(field, a, bx, by);
    }
    a = gwy_data_field_area_get_avg_mask(field, mask, GWY_MASK_INCLUDE, 0, 0, xres, yres);
    gwy_data_field_add(field, -a);
}

static void
compute_slopes(GwyDataField *field, gint kernel_size,
               GwyDataField *xder, GwyDataField *yder, GwyDataField *atanfield)
{
    gint xres = gwy_data_field_get_xres(field), yres = gwy_data_field_get_yres(field);
    GwyPlaneFitQuantity quantites[2] = { GWY_PLANE_FIT_BX, GWY_PLANE_FIT_BY };
    GwyDataField *fields[2] = { xder, yder };
    const gdouble *xd, *yd;
    gdouble *a;
    gint i, n = xres*yres;

    if (!kernel_size)
        gwy_data_field_filter_slope(field, xder, yder);
    else {
        gwy_data_field_fit_local_planes(field, kernel_size, 2, quantites, fields);
        gwy_data_field_multiply(xder, 1.0/gwy_data_field_get_dx(field));
        gwy_data_field_multiply(yder, 1.0/gwy_data_field_get_dy(field));
    }

    xd = gwy_data_field_get_data_const(xder);
    yd = gwy_data_field_get_data_const(yder);
    a = gwy_data_field_get_data(atanfield);
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(a,xd,yd,n)
#endif
    for (i = 0; i < n; i++) {
        a[i] = gwy_canonicalize_angle(atan2(-yd[i], xd[i]), TRUE, TRUE);
    }
}

static gdouble
find_prevalent_phi(GwyDataField *field, GwyDataField *exclmask,
                   GwyDataField *xder, GwyDataField *yder, GwyDataField *atanfield)
{
    const gdouble *xd = gwy_data_field_get_data_const(xder);
    const gdouble *yd = gwy_data_field_get_data_const(yder);
    const gdouble *m = gwy_data_field_get_data_const(exclmask);
    const gdouble *a = gwy_data_field_get_data_const(atanfield);
    gint xres = gwy_data_field_get_xres(field), yres = gwy_data_field_get_yres(field);
    GwyDataLine *phidist;
    gdouble *data;
    gdouble d, phi, x, y[3];
    gint n, i, iphi, size;

    n = xres*yres;
    gwy_data_field_area_count_in_range(exclmask, NULL, 0, 0, xres, yres, 0.5, 0.5, &size, NULL);
    size = (gint)floor(5.49*cbrt(size) + 0.5);
    size = MAX(size, 24);
    gwy_debug("phi dist size %d", size);

    phidist = gwy_data_line_new(size, 2.0*G_PI, TRUE);
    data = gwy_data_line_get_data(phidist);
    for (i = 0; i < n; i++) {
        if (m[i] <= 0.0) {
            d = xd[i]*xd[i] + yd[i]*yd[i];
            iphi = floor(size*a[i]/(2.0*G_PI));
            iphi = CLAMP(iphi, 0, size-1);
            data[iphi] += sqrt(d);
        }
    }
    i = (gint)gwy_data_line_max_pos_i(phidist);

    gwy_debug("phi maximum at %d (%g deg)", i, 360.0*(i + 0.5)/size);
    y[0] = gwy_data_line_get_val(phidist, (i + size-1) % size);
    y[1] = gwy_data_line_get_val(phidist, i);
    y[2] = gwy_data_line_get_val(phidist, (i+1) % size);
    x = 0.0;
    gwy_math_refine_maximum_1d(y, &x);
    phi = gwy_data_line_get_dx(phidist)*(i + 0.5 + x);
    gwy_debug("refined phi to %g deg", phi*180.0/G_PI);

    g_object_unref(phidist);

    return gwy_canonicalize_angle(phi + G_PI, TRUE, TRUE);
}

static void
find_imprint_side_centre(GwyDataField *field,
                         gdouble search_to_z, gdouble phi,
                         gdouble *xcentre, gdouble *ycentre)
{
    gint xres = gwy_data_field_get_xres(field), yres = gwy_data_field_get_yres(field);
    gdouble x, y, cphi = cos(phi), sphi = sin(phi);
    const gdouble *d = gwy_data_field_get_data_const(field);
    gint i, j, k;

    gwy_debug("direction %g deg", 180.0/G_PI*phi);

    /* Go from the minimum until we reach search_to_z. */
    k = 1;
    while (TRUE) {
        x = *xcentre - 0.5*cphi*k;
        y = *ycentre + 0.5*sphi*k;
        j = (gint)floor(x);
        i = (gint)floor(y);
        if (i < 0 || i >= yres || j < 0 || j >= xres)
            break;
        if (d[i*xres + j] >= search_to_z)
            break;
        k++;
    }

    k--;
    *xcentre = *xcentre - 0.5*cphi*k;
    *ycentre = *ycentre + 0.5*sphi*k;
}

static void
calc_mean_normal(GwyDataField *xder, GwyDataField *yder, gdouble x, gdouble y, gdouble r, GwyXYZ *v)
{
    const gdouble *xd = gwy_data_field_get_data_const(xder);
    const gdouble *yd = gwy_data_field_get_data_const(yder);
    gint xres = gwy_data_field_get_xres(xder), yres = gwy_data_field_get_yres(yder);
    gint j = (gint)floor(x);
    gint i = (gint)floor(y);
    gint ii, jj, n, k;
    gdouble s;

    /* Calculate average normal around the point. */
    gwy_clear(v, 1);
    n = 0;
    for (ii = -r-1; ii <= r+1; ii++) {
        if (ii + i < 0 || ii + i >= yres)
            continue;
        for (jj = -r-1; jj <= r+1; jj++) {
            if (jj + j < 0 || jj + j >= xres)
                continue;
            if ((jj + j - x)*(jj + j - x) + (ii + i - y)*(ii + i - y) > r)
                continue;

            k = (ii + i)*xres + (jj + j);
            v->x += xd[k];
            v->y += yd[k];
            v->z += 1.0;
            n++;
        }
    }
    s = sqrt(v->x*v->x + v->y*v->y + v->z*v->z);
    v->x /= s;
    v->y /= s;
    v->z /= s;
    gwy_debug("mean local normal (%g,%g,%g) direction %g deg (%d samples)",
              v->x, v->y, v->z, 180.0/G_PI*gwy_canonicalize_angle(atan2(-v->y, v->x), TRUE, TRUE), n);
}

static void
mark_facet(GwyDataField *xder, GwyDataField *yder, GwyDataField *mask,
           gdouble theta0, gdouble phi0, gdouble tolerance)
{
    gdouble ctol = cos(tolerance), cth0 = cos(theta0), sth0 = sin(theta0), cphi0 = cos(phi0), sphi0 = sin(phi0);
    const gdouble *xd = gwy_data_field_get_data_const(xder);
    const gdouble *yd = gwy_data_field_get_data_const(yder);
    gdouble *md = gwy_data_field_get_data(mask);
    guint i, n = gwy_data_field_get_xres(xder)*gwy_data_field_get_yres(xder);

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(xd,yd,md,n,cth0,sth0,cphi0,sphi0,ctol)
#endif
    for (i = 0; i < n; i++) {
        gdouble stheta2 = xd[i]*xd[i] + yd[i]*yd[i];
        gdouble stheta = sqrt(stheta2);
        gdouble ctheta = sqrt(1.0 - fmin(stheta2, 1.0));
        gdouble cphi = xd[i]/stheta, sphi = yd[i]/stheta;
        gdouble cro = cth0*ctheta + sth0*stheta*(cphi*cphi0 + sphi*sphi0);
        md[i] = (cro >= ctol);
    }
}

#if 0
static gboolean
fit_pyramid(GwyDataField *field, GwyDataField *impfaces,
            GwyIndenterType indenter, PyramidParameters *pyrpar)
{
    static const GwyEnum indenters[] = {
        //{ N_("Sphere"),              INDENTER_SPHERE,   },
        { N_("Pyramid (3-sided)"),   INDENTER_PYRAMID3, },
        { N_("Pyramid (rectangle)"), INDENTER_PYRAMID4, },
    };
    const gchar *name;
    GwyShapeFitPreset *preset;
    GwyNLFitter *fitter = NULL;
    GwySurface *surface = NULL;
    gdouble *params = NULL;
    gint npoints, nparams, i, nsides = indenter;
    const GwyXYZ *points;
    gdouble rss, L, a, phi;
    gboolean ok = FALSE;

    if (!(name = gwy_enum_to_string(indenter, indenters, G_N_ELEMENTS(indenters))))
        return FALSE;
    if (!(preset = gwy_inventory_get_item(gwy_shape_fit_presets(), name)))
        return FALSE;

    surface = gwy_surface_new();
    gwy_surface_set_from_data_field(surface, field);
    points = gwy_surface_get_data_const(surface);
    npoints = gwy_surface_get_npoints(surface);
    nparams = gwy_shape_fit_preset_get_nparams(preset);
    params = g_new(gdouble, nparams);
    gwy_shape_fit_preset_setup(preset, points, npoints, params);
    if (!gwy_shape_fit_preset_guess(preset, points, npoints, params)) {
        gwy_debug("guess failed");
        goto end;
    }
    gwy_debug("guess apex (%g,%g) L=%g, h=%g, phi=%g", params[0], params[1], params[4], params[3], params[6]);
    fitter = gwy_shape_fit_preset_quick_fit(preset, NULL, points, npoints, params, NULL, &rss);
    if (!fitter || rss < 0.0) {
        gwy_debug("quick fit failed");
        goto end;
    }
    gwy_debug("quick apex (%g,%g) L=%g, h=%g, phi=%g", params[0], params[1], params[4], params[3], params[6]);

    pyrpar->apex.x = params[0];
    pyrpar->apex.y = params[1];
    L = params[3];
    a = params[5];
    phi = params[6];
    for (i = 0; i < nsides; i++) {
    }

    ok = TRUE;

end:
    if (fitter)
        gwy_math_nlfit_free(fitter);
    GWY_OBJECT_UNREF(surface);
    g_free(params);

    return ok;
}
#endif

/* Keep connected components not too far from imprint in pileup. */
static void
mark_pileup(GwyDataField *above, GwyDataField *imprint, GwyDataField *pileup)
{
    gint xres = gwy_data_field_get_xres(imprint), yres = gwy_data_field_get_yres(imprint);
    gint ngrains, i, n = xres*yres;
    gint *grains, *keep;
    gdouble *d;
    gdouble maxdist;

    maxdist = fmax(0.02*sqrt(gwy_data_field_get_sum(imprint)), 2.1);
    gwy_debug("using maxdist %g", maxdist);

    gwy_data_field_copy(imprint, pileup, FALSE);
    gwy_data_field_grains_invert(pileup);
    gwy_data_field_grain_simple_dist_trans(pileup, GWY_DISTANCE_TRANSFORM_EUCLIDEAN, FALSE);
    d = gwy_data_field_get_data(pileup);

    grains = g_new0(gint, n);
    ngrains = gwy_data_field_number_grains(above, grains);
    keep = g_new0(gint, ngrains+1);
    for (i = 0; i < n; i++)
        keep[grains[i]] |= (d[i] > 0.0 && d[i] < maxdist);
    keep[0] = 0;
    for (i = 0; i < n; i++)
        d[i] = keep[grains[i]];

    g_free(keep);
    g_free(grains);
}

static void
calc_surface_and_volume(GwyDataField *field, GwyDataField *mask,
                        gdouble *Aproj, gdouble *Asurf, gdouble *V)
{
    gint xres = gwy_data_field_get_xres(field), yres = gwy_data_field_get_yres(field);
    gdouble dA = gwy_data_field_get_dx(field)*gwy_data_field_get_dy(field);

    if (Aproj)
        *Aproj = dA*gwy_data_field_get_sum(mask);
    if (Asurf)
        *Asurf = gwy_data_field_area_get_surface_area_mask(field, mask, GWY_MASK_INCLUDE, 0, 0, xres, yres);
    if (V)
        *V = gwy_data_field_area_get_volume(field, NULL, mask, 0, 0, xres, yres);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
