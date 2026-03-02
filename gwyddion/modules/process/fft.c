/*
 *  $Id: fft.c 24116 2021-09-13 15:37:17Z yeti-dn $
 *  Copyright (C) 2003-2021 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/arithmetic.h>
#include <libprocess/inttrans.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    OUTPUT_REAL    = (1 << 0),
    OUTPUT_IMAG    = (1 << 1),
    OUTPUT_MODULUS = (1 << 2),
    OUTPUT_PHASE   = (1 << 3),
} OutputFlags;

enum {
    PARAM_INVERSE_TRANSFORM,
    PARAM_OUT,
    PARAM_PRESERVERMS,
    PARAM_RAW_TRANSFORM,
    PARAM_USE_IMAG_PART,
    PARAM_WINDOW,
    PARAM_ZEROMEAN,
    PARAM_IMAG_PART,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             fft                 (GwyContainer *data,
                                             GwyRunType runtype);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static gboolean         imagpart_filter     (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);
static void             create_output       (GwyContainer *data,
                                             gint id,
                                             GwyDataField *field,
                                             const gchar *output_name,
                                             gboolean itransform,
                                             gboolean is_phase);
static GwyDataField*    make_modulus        (GwyDataField *re,
                                             GwyDataField *im);
static GwyDataField*    make_phase          (GwyDataField *re,
                                             GwyDataField *im);
static void             sanitise_params     (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Two-dimensional FFT (Fast Fourier Transform)."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

GWY_MODULE_QUERY2(module_info, fft)

static gboolean
module_register(void)
{
    gwy_process_func_register("fft",
                              (GwyProcessFunc)&fft,
                              N_("/_Integral Transforms/2D _FFT..."),
                              GWY_STOCK_FFT,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Compute Fast Fourier Transform"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum outputs[] = {
        { N_("Real"),      OUTPUT_REAL,    },
        { N_("Imaginary"), OUTPUT_IMAG,    },
        { N_("Modulus"),   OUTPUT_MODULUS, },
        { N_("Phase"),     OUTPUT_PHASE,   },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_boolean(paramdef, PARAM_INVERSE_TRANSFORM, "inverse_transform", _("_Inverse transform"), FALSE);
    gwy_param_def_add_gwyflags(paramdef, PARAM_OUT, "out", _("Output _type"),
                               outputs, G_N_ELEMENTS(outputs), OUTPUT_MODULUS);
    gwy_param_def_add_boolean(paramdef, PARAM_PRESERVERMS, "preserverms", _("_Preserve RMS"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_RAW_TRANSFORM, "raw_transform", _("Ra_w transform"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_USE_IMAG_PART, "use_imagpart", NULL, FALSE);
    gwy_param_def_add_enum(paramdef, PARAM_WINDOW, "window", NULL, GWY_TYPE_WINDOWING_TYPE, GWY_WINDOWING_HANN);
    gwy_param_def_add_boolean(paramdef, PARAM_ZEROMEAN, "zeromean", _("Subtract mean _value beforehand"), TRUE);
    gwy_param_def_add_image_id(paramdef, PARAM_IMAG_PART, "imagpart", _("I_maginary part"));
    return paramdef;
}

static void
fft(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *field, *imagpart, *raout, *ipout;
    GwyDialogOutcome outcome;
    ModuleArgs args;
    GwyParams *params;
    gboolean is_inv, raw_transform, use_imagpart, humanize = TRUE;
    OutputFlags out;
    gint id, datano;

    g_return_if_fail(runtype & RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_CONTAINER_ID, &datano,
                                     0);
    g_return_if_fail(field);

    args.field = field;
    args.params = params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    out = gwy_params_get_flags(params, PARAM_OUT);
    if (!out)
        goto end;

    raout = gwy_data_field_new_alike(field, FALSE);
    ipout = gwy_data_field_new_alike(field, FALSE);

    use_imagpart = gwy_params_get_boolean(params, PARAM_USE_IMAG_PART);
    raw_transform = gwy_params_get_boolean(params, PARAM_RAW_TRANSFORM);
    is_inv = gwy_params_get_boolean(params, PARAM_INVERSE_TRANSFORM) && raw_transform;
    imagpart = use_imagpart ? gwy_params_get_image(params, PARAM_IMAG_PART) : NULL;

    if (is_inv) {
        GwyDataField *rein = gwy_data_field_duplicate(field);
        GwyDataField *imin = imagpart ? gwy_data_field_duplicate(imagpart) : NULL;

        gwy_data_field_2dfft_dehumanize(rein);
        gwy_data_field_fft_postprocess(rein, FALSE);
        if (imin) {
            gwy_data_field_2dfft_dehumanize(imin);
            gwy_data_field_fft_postprocess(imin, FALSE);
        }
        gwy_data_field_2dfft_raw(rein, imin, raout, ipout, GWY_TRANSFORM_DIRECTION_BACKWARD);
        g_object_unref(rein);
        GWY_OBJECT_UNREF(imin);
        humanize = FALSE;
    }
    else if (raw_transform)
        gwy_data_field_2dfft_raw(field, imagpart, raout, ipout, GWY_TRANSFORM_DIRECTION_FORWARD);
    else {
        gboolean preserverms = gwy_params_get_boolean(params, PARAM_PRESERVERMS);
        gboolean zeromean = gwy_params_get_boolean(params, PARAM_ZEROMEAN);
        GwyWindowingType window = gwy_params_get_enum(params, PARAM_WINDOW);
        gwy_data_field_2dfft(field, imagpart, raout, ipout,
                             window, GWY_TRANSFORM_DIRECTION_FORWARD, GWY_INTERPOLATION_LINEAR,  /* ignored */
                             preserverms, zeromean ? 1 : 0);
    }
    gwy_data_field_fft_postprocess(raout, humanize);
    gwy_data_field_fft_postprocess(ipout, humanize);

    if (out & OUTPUT_REAL)
        create_output(data, id, g_object_ref(raout), _("FFT Real"), is_inv, FALSE);
    if (out & OUTPUT_IMAG)
        create_output(data, id, g_object_ref(ipout), _("FFT Imaginary"), is_inv, FALSE);
    if (out & OUTPUT_MODULUS)
        create_output(data, id, make_modulus(raout, ipout), _("FFT Modulus"), is_inv, FALSE);
    if (out & OUTPUT_PHASE)
        create_output(data, id, make_phase(raout, ipout), _("FFT Phase"), is_inv, TRUE);

    g_object_unref(raout);
    g_object_unref(ipout);

end:
    g_object_unref(params);
}

static void
create_output(GwyContainer *data, gint id, GwyDataField *field, const gchar *output_name,
              gboolean itransform, gboolean is_phase)
{
    gint newid;

    newid = gwy_app_data_browser_add_data_field(field, data, TRUE);
    gwy_app_set_data_field_title(data, newid, output_name);
    gwy_app_channel_log_add_proc(data, id, newid);
    g_object_unref(field);

    if (itransform)
        return;

    /* make fft more visible by choosing a good gradient and using auto range */
    gwy_container_set_const_string(data, gwy_app_get_data_palette_key_for_id(newid), "DFit");
    if (!is_phase)
        gwy_container_set_enum(data, gwy_app_get_data_range_type_key_for_id(newid), GWY_LAYER_BASIC_RANGE_AUTO);
}

static GwyDataField*
make_modulus(GwyDataField *re, GwyDataField *im)
{
    GwyDataField *modulus;
    const gdouble *datare, *dataim;
    gdouble *data;
    guint n, i;

    modulus = gwy_data_field_new_alike(re, FALSE);
    n = gwy_data_field_get_xres(re)*gwy_data_field_get_yres(re);
    datare = gwy_data_field_get_data_const(re);
    dataim = gwy_data_field_get_data_const(im);
    data = gwy_data_field_get_data(modulus);
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(data,datare,dataim,n) \
            private(i)
#endif
    for (i = 0; i < n; i++)
        data[i] = sqrt(datare[i]*datare[i] + dataim[i]*dataim[i]);

    return modulus;
}

static GwyDataField*
make_phase(GwyDataField *re, GwyDataField *im)
{
    GwyDataField *phase;
    const gdouble *datare, *dataim;
    gdouble *data;
    guint n, i;

    phase = gwy_data_field_new_alike(re, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(phase), NULL);
    n = gwy_data_field_get_xres(re)*gwy_data_field_get_yres(re);
    datare = gwy_data_field_get_data_const(re);
    dataim = gwy_data_field_get_data_const(im);
    data = gwy_data_field_get_data(phase);
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(data,datare,dataim,n) private(i)
#endif
    for (i = 0; i < n; i++)
        data[i] = atan2(dataim[i], datare[i]);

    return phase;
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gui.args = args;
    gui.dialog = gwy_dialog_new(_("2D FFT"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("2D FFT"));
    gwy_param_table_append_checkbox(table, PARAM_RAW_TRANSFORM);
    gwy_param_table_append_image_id(table, PARAM_IMAG_PART);
    gwy_param_table_data_id_set_filter(table, PARAM_IMAG_PART, imagpart_filter, args->field, NULL);
    gwy_param_table_add_enabler(table, PARAM_USE_IMAG_PART, PARAM_IMAG_PART);
    gwy_param_table_append_checkbox(table, PARAM_INVERSE_TRANSFORM);

    gwy_param_table_append_header(table, -1, _("Output"));
    gwy_param_table_append_checkboxes(table, PARAM_OUT);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_combo(table, PARAM_WINDOW);
    gwy_param_table_append_checkbox(table, PARAM_ZEROMEAN);
    gwy_param_table_append_checkbox(table, PARAM_PRESERVERMS);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_RAW_TRANSFORM) {
        gboolean is_raw = gwy_params_get_boolean(params, PARAM_RAW_TRANSFORM);
        gwy_param_table_set_sensitive(table, PARAM_PRESERVERMS, !is_raw);
        gwy_param_table_set_sensitive(table, PARAM_ZEROMEAN, !is_raw);
        gwy_param_table_set_sensitive(table, PARAM_WINDOW, !is_raw);
        gwy_param_table_set_sensitive(table, PARAM_INVERSE_TRANSFORM, is_raw);
    }
}

static gboolean
imagpart_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *imagpart, *field = (GwyDataField*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &imagpart))
        return FALSE;
    return !gwy_data_field_check_compatibility(imagpart, field, GWY_DATA_COMPATIBILITY_ALL);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyAppDataId imagpart = gwy_params_get_data_id(params, PARAM_IMAG_PART);
    gboolean is_none = gwy_params_data_id_is_none(params, PARAM_IMAG_PART);
    gboolean use_imagpart = gwy_params_get_boolean(params, PARAM_USE_IMAG_PART);

    if (use_imagpart
        && (is_none || !imagpart_filter(gwy_app_data_browser_get(imagpart.datano), imagpart.id, args->field)))
        gwy_params_set_boolean(params, PARAM_USE_IMAG_PART, FALSE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
