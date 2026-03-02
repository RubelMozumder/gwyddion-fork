/*
 *  @(#) $Id: hertz.c 25638 2023-09-07 12:23:28Z yeti-dn $
 *  Copyright (C) 2017-2021 Anna Charvatova Campbell
 *  E-mail: acampbellova@cmi.cz
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
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libgwymodule/gwymodule.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libprocess/arithmetic.h>
#include <libgwydgets/gwydgets.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES  (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum  {
    PREVIEW_MEANCURV    = 0,
    PREVIEW_GAUSSCURV   = 1,
    PREVIEW_MODULUS     = 2,
    PREVIEW_DEFORMATION = 3,
    PREVIEW_MASK        = 4,
    PREVIEW_NTYPES
} HertzPreviewType;

enum {
    PARAM_MODULUS, /* in Pa */
    PARAM_RADIUS,  /* in m */
    PARAM_LOAD,    /* in N */
    PARAM_PREVIEW,
    PARAM_UPDATE,
    PARAM_MASK_COLOR,
    LABEL_BAD_UNITS,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result[PREVIEW_NTYPES];
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
    GtkWidget *view;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             hertz_modulus       (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    "Calculates the apparent Young's modulus of a rough surface according to Hertzian contact theory.",
    "Anna Charvatova Campbell <acampbellova@cmi.cz>",
    "0.2",
    "Anna Charvatova Campbell",
    "2017",
};

GWY_MODULE_QUERY2(module_info, hertz)

static gboolean
module_register(void)
{
    gwy_process_func_register("hertz_modulus",
                              (GwyProcessFunc)&hertz_modulus,
                              N_("/SPM M_odes/_Force and Indentation/_Hertz contact..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Hertzian contact theory"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum previews[] = {
        { N_("Mean _curvature"),     PREVIEW_MEANCURV,    },
        { N_("Gaussian c_urvature"), PREVIEW_GAUSSCURV,   },
        { N_("Contact _modulus"),    PREVIEW_MODULUS,     },
        { N_("_Deformation"),        PREVIEW_DEFORMATION, },
        { N_("Excluded _points"),    PREVIEW_MASK,        },
    };

    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "hertzcontact");
    gwy_param_def_add_double(paramdef, PARAM_MODULUS, "modulus", _("_Contact modulus"), 1e6, 1e12, 13e9);
    gwy_param_def_add_double(paramdef, PARAM_LOAD, "load", _("_Load applied"), 1e-7, 1.0, 1e-6);
    gwy_param_def_add_double(paramdef, PARAM_RADIUS, "radius", _("_Tip radius"), G_MINDOUBLE, G_MAXDOUBLE, 42e-9);
    gwy_param_def_add_gwyenum(paramdef, PARAM_PREVIEW, "preview", gwy_sgettext("verb|Display"),
                              previews, G_N_ELEMENTS(previews), PREVIEW_MEANCURV);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, FALSE);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    return paramdef;
}

static void
hertz_modulus(GwyContainer *data, GwyRunType runtype)
{
    static const GwyEnum outputs[] = {
        { N_("Mean curvature"),              PREVIEW_MEANCURV,    },
        { N_("Gaussian curvature"),          PREVIEW_GAUSSCURV,   },
        { N_("Hertzian contact modulus"),    PREVIEW_MODULUS,     },
        { N_("Hertzian theory deformation"), PREVIEW_DEFORMATION, },
    };
    static const GwyEnum result_units[] = {
        { "1/m",   PREVIEW_MEANCURV,    },
        { "1/m^2", PREVIEW_GAUSSCURV,   },
        { "Pa",    PREVIEW_MODULUS,     },
        { "m",     PREVIEW_DEFORMATION, },
        { "",      PREVIEW_MASK,        },
    };
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GwyDataField *maskcopy;
    ModuleArgs args;
    HertzPreviewType i;
    const gchar *s;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    if (!gwy_require_image_same_units(args.field, data, id, _("Hertzian Contact Modulus")))
        return;

    for (i = 0; i < PREVIEW_NTYPES; i++) {
        args.result[i] = gwy_data_field_new_alike(args.field, TRUE);
        s = gwy_enum_to_string(i, result_units, G_N_ELEMENTS(result_units));
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result[i]), s);
    }
    args.mask = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.mask), NULL);

    args.params = gwy_params_new_from_settings(define_module_params());
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    for (i = 0; i < PREVIEW_NTYPES; i++) {
        if (i == PREVIEW_MASK)
            continue;

        newid = gwy_app_data_browser_add_data_field(args.result[i], data, TRUE);
        maskcopy = gwy_data_field_duplicate(args.mask);
        gwy_container_pass_object(data, gwy_app_get_mask_key_for_id(newid), maskcopy);
        s = gwy_enum_to_string(i, outputs, G_N_ELEMENTS(outputs));
        gwy_app_set_data_field_title(data, newid, gwy_sgettext(s));
        gwy_app_channel_log_add(data, id, newid, "proc::hertz_modulus",
                                "settings-name", "hertzcontact",
                                NULL);
    }

end:
    for (i = 0; i < PREVIEW_NTYPES; i++)
        g_object_unref(args.result[i]);
    g_object_unref(args.mask);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkWidget *hbox;
    ModuleGUI gui;
    GwySIUnit *unit;
    GwySIValueFormat *vf;
    HertzPreviewType i;
    gdouble h;

    gui.args = args;
    gui.data = gwy_container_new();
    for (i = 0; i < PREVIEW_NTYPES; i++) {
        gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(i), args->result[i]);
        gwy_app_sync_data_items(data, gui.data, id, i, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_MASK_COLOR,
                                0);
    }
    gwy_container_set_object(gui.data, gwy_app_get_mask_key_for_id(0), args->mask);
    unit = gwy_data_field_get_si_unit_xy(args->field);
    vf = gwy_data_field_get_value_format_xy(args->field, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
    h = fmin(gwy_data_field_get_dx(args->field), gwy_data_field_get_dy(args->field));

    gui.dialog = gwy_dialog_new(_("Hertzian Contact Modulus"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    gui.view = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(gui.view), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    if (!gwy_si_unit_equal_string(unit, "m")) {
        gwy_param_table_append_message(table, LABEL_BAD_UNITS,
                                       _("Values should be height (meters).\n"
                                         "The following results do not make much sense."));
        gwy_param_table_message_set_type(table, LABEL_BAD_UNITS, GTK_MESSAGE_ERROR);
    }

    gwy_param_table_append_slider(table, PARAM_MODULUS);
    gwy_param_table_slider_set_factor(table, PARAM_MODULUS, 1e-9);
    gwy_param_table_set_unitstr(table, PARAM_MODULUS, "GPa");
    gwy_param_table_slider_set_mapping(table, PARAM_MODULUS, GWY_SCALE_MAPPING_LOG);

    gwy_param_table_append_slider(table, PARAM_RADIUS);
    gwy_param_table_slider_restrict_range(table, PARAM_RADIUS, 0.05*h, 500*h);
    gwy_param_table_slider_set_factor(table, PARAM_RADIUS, 1.0/vf->magnitude);
    gwy_param_table_set_unitstr(table, PARAM_RADIUS, vf->units);
    gwy_param_table_slider_set_mapping(table, PARAM_RADIUS, GWY_SCALE_MAPPING_LOG);

    gwy_param_table_append_slider(table, PARAM_LOAD);
    gwy_param_table_slider_set_factor(table, PARAM_LOAD, 1e6);
    gwy_param_table_set_unitstr(table, PARAM_LOAD, "µN");
    gwy_param_table_slider_set_mapping(table, PARAM_LOAD, GWY_SCALE_MAPPING_LOG);

    gwy_param_table_append_radio(table, PARAM_PREVIEW);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, data, id);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    gwy_si_unit_value_format_free(vf);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;

    if (id < 0 || id == PARAM_PREVIEW) {
        HertzPreviewType display = gwy_params_get_enum(params, PARAM_PREVIEW);
        GwyPixmapLayer *layer = gwy_data_view_get_base_layer(GWY_DATA_VIEW(gui->view));
        gwy_pixmap_layer_set_data_key(layer, g_quark_to_string(gwy_app_get_data_key_for_id(display)));
    }

    if (id != PARAM_UPDATE && id != PARAM_PREVIEW && id != PARAM_MASK_COLOR)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    guint i;

    execute(args);
    for (i = 0; i < PREVIEW_NTYPES; i++)
        gwy_data_field_data_changed(args->result[i]);
    gwy_data_field_data_changed(args->mask);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args)
{
    gdouble R = gwy_params_get_double(args->params, PARAM_RADIUS);
    gdouble E = gwy_params_get_double(args->params, PARAM_MODULUS);
    gdouble F = gwy_params_get_double(args->params, PARAM_LOAD);
    GwyDataField *meancurv = args->result[PREVIEW_MEANCURV];
    GwyDataField *gausscurv = args->result[PREVIEW_GAUSSCURV];
    GwyDataField *modulus = args->result[PREVIEW_MODULUS];
    GwyDataField *dz = args->result[PREVIEW_DEFORMATION];
    GwyDataField *mask = args->mask, *field = args->field;
    GwyDataField *dxfield, *dyfield;
    GwyDataField *dxxfield, *dxyfield, *dyyfield;
    gdouble *px, *py, *pxx, *pyy, *pxy;
    gdouble *pc, *pg, *pE, *m, *pz;
    gint i, xres, yres, n;
    gdouble coeff;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    n = xres*yres;

    /* Recycle modulus and dz as the first derivative fiels. */
    dxfield = modulus;
    dyfield = dz;
    gwy_data_field_filter_slope(field, dxfield, dyfield);

    dxxfield = gwy_data_field_new_alike(field, FALSE);
    dyyfield = gwy_data_field_new_alike(field, FALSE);
    dxyfield = gwy_data_field_new_alike(field, FALSE);
    gwy_data_field_filter_slope(dxfield, dxxfield, dxyfield);
    /* Recycle mask as temporary dyxfield and then average. */
    gwy_data_field_filter_slope(dyfield, mask, dyyfield);
    gwy_data_field_linear_combination(dxyfield, 0.5, dxyfield, 0.5, mask, 0.0);

    px = gwy_data_field_get_data(dxfield);
    py = gwy_data_field_get_data(dyfield);
    pxx = gwy_data_field_get_data(dxxfield);
    pxy = gwy_data_field_get_data(dxyfield);
    pyy = gwy_data_field_get_data(dyyfield);

    pc = gwy_data_field_get_data(meancurv);
    pg = gwy_data_field_get_data(gausscurv);
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(px,py,pxx,pxy,pyy,pc,pg,n)
#endif
    for (i = 0; i < n; i++) {
        gdouble dx2 = px[i]*px[i], dy2 = py[i]*py[i], dxdy = px[i]*py[i];
        gdouble w = 1.0 + dx2 + dy2;
        pc[i] = 0.5*((1.0 + dx2)*pyy[i] + (1.0 + dy2)*pxx[i] - 2.0*pxy[i]*dxdy)/(w*sqrt(w));
        pg[i] = (pxx[i]*pyy[i] - pxy[i]*pxy[i])/(w*w);
    }

    g_object_unref(dxxfield);
    g_object_unref(dxyfield);
    g_object_unref(dyyfield);

    gwy_data_field_clear(mask);
    pE = gwy_data_field_get_data(modulus);
    pz = gwy_data_field_get_data(dz);
    m = gwy_data_field_get_data(mask);
    coeff = cbrt(9.0/16.0*F*F/R);
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(m,pE,pz,pc,pg,R,E,coeff,n)
#endif
    for (i = 0; i < n; i++) {
        gdouble d = 1.0 - 2.0*pc[i]*R + R*R*pg[i];
        if (d <= 0.0) {
            m[i] = 1.0;
            pE[i] = -1.0;
            pz[i] = -1e-9;
        }
        else {
            pE[i] = E/sqrt(sqrt(d));
            pz[i] = coeff/cbrt(pE[i]*pE[i]);
        }
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
