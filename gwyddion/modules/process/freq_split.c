/*
 *  $Id: freq_split.c 24365 2021-10-13 12:45:57Z yeti-dn $
 *  Copyright (C) 2018-2021 David Necas (Yeti).
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/arithmetic.h>
#include <libprocess/inttrans.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    FSPLIT_PREVIEW_ORIGINAL  = 0,
    FSPLIT_PREVIEW_LOW_PASS  = 1,
    FSPLIT_PREVIEW_HIGH_PASS = 2,
} FSplitPreviewType;

typedef enum {
    FSPLIT_BOUNDARY_NONE    = 0,
    FSPLIT_BOUNDARY_LAPLACE = 1,
    FSPLIT_BOUNDARY_SMCONN  = 2,
    FSPLIT_BOUNDARY_MIRROR  = 3,
} FSplitBoundaryType;

typedef enum {
    FSPLIT_OUTPUT_LOW_PASS  = (1 << 0),
    FSPLIT_OUTPUT_HIGH_PASS = (1 << 1),
    FSPLIT_OUTPUT_BOTH      = (FSPLIT_OUTPUT_LOW_PASS | FSPLIT_OUTPUT_HIGH_PASS),
} FSplitOutputType;

enum {
    PARAM_CUTOFF,
    PARAM_WIDTH,
    PARAM_BOUNDARY,
    PARAM_OUTPUT,
    PARAM_PREVIEW,
    PARAM_UPDATE,
    INFO_WAVELENGTH,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *highpass;
    GwyDataField *lowpass;
    /* Cached expensive intermediate calculation. */
    GwyDataField *extfftre;
    GwyDataField *extfftim;
    gint leftext;
    gint topext;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
    GwySIValueFormat *vf;
} ModuleGUI;

static gboolean         module_register      (void);
static GwyParamDef*     define_module_params (void);
static void             freq_split           (GwyContainer *data,
                                              GwyRunType runtype);
static void             execute              (ModuleArgs *args);
static GwyDialogOutcome run_gui              (ModuleArgs *args,
                                              GwyContainer *data,
                                              gint id);
static void             param_changed        (ModuleGUI *gui,
                                              gint id);
static void             preview              (gpointer user_data);
static void             update_real_frequency(ModuleGUI *gui);
static void             extend_and_fft       (GwyDataField *field,
                                              GwyDataField **extfftre,
                                              GwyDataField **extfftim,
                                              gint *leftext,
                                              gint *topext,
                                              FSplitBoundaryType boundary);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Splits image into low and high frequency components."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, freq_split)

static gboolean
module_register(void)
{
    gwy_process_func_register("freq_split",
                              (GwyProcessFunc)&freq_split,
                              N_("/_Level/_Frequency Split..."),
                              GWY_STOCK_FREQUENCY_SPLIT,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Split into low and high frequencies"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum boundaries[] = {
        { N_("boundary-handling|None"), FSPLIT_BOUNDARY_NONE,    },
        { N_("Laplace"),                FSPLIT_BOUNDARY_LAPLACE, },
        { N_("Smooth connect"),         FSPLIT_BOUNDARY_SMCONN,  },
        { N_("Mirror"),                 FSPLIT_BOUNDARY_MIRROR,  },
    };
    static const GwyEnum previews[] = {
        { N_("Data"),      FSPLIT_PREVIEW_ORIGINAL,  },
        { N_("High-pass"), FSPLIT_PREVIEW_HIGH_PASS, },
        { N_("Low-pass"),  FSPLIT_PREVIEW_LOW_PASS,  },
    };
    static const GwyEnum outputs[] = {
        { N_("Low-pass image"),  FSPLIT_OUTPUT_LOW_PASS  },
        { N_("High-pass image"), FSPLIT_OUTPUT_HIGH_PASS },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_double(paramdef, PARAM_CUTOFF, "cutoff", _("C_ut-off"), 0.0, 0.3, 0.3);
    gwy_param_def_add_double(paramdef, PARAM_WIDTH, "width", _("_Edge width"), 0.0, 0.2, 0.03);
    gwy_param_def_add_gwyenum(paramdef, PARAM_BOUNDARY, "boundary", _("_Boundary treatment"),
                              boundaries, G_N_ELEMENTS(boundaries), FSPLIT_BOUNDARY_NONE);
    gwy_param_def_add_gwyflags(paramdef, PARAM_OUTPUT, "output", _("Output type"),
                               outputs, G_N_ELEMENTS(outputs), FSPLIT_OUTPUT_BOTH);
    gwy_param_def_add_gwyenum(paramdef, PARAM_PREVIEW, "preview", gwy_sgettext("verb|Display"),
                              previews, G_N_ELEMENTS(previews), FSPLIT_PREVIEW_HIGH_PASS);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    return paramdef;
}

static void
freq_split(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    FSplitOutputType output;
    ModuleArgs args;
    GwyParams *params;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);
    args.highpass = gwy_data_field_new_alike(args.field, TRUE);
    args.lowpass = gwy_data_field_new_alike(args.field, TRUE);
    args.params = params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    output = gwy_params_get_flags(params, PARAM_OUTPUT);

    if (output & FSPLIT_OUTPUT_LOW_PASS) {
        newid = gwy_app_data_browser_add_data_field(args.lowpass, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                0);
        gwy_app_set_data_field_title(data, newid, _("Low-pass"));
        gwy_app_channel_log_add_proc(data, id, newid);
    }

    if (output & FSPLIT_OUTPUT_HIGH_PASS) {
        newid = gwy_app_data_browser_add_data_field(args.highpass, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                0);
        gwy_app_set_data_field_title(data, newid, _("High-pass"));
        gwy_app_channel_log_add_proc(data, id, newid);
    }

end:
    GWY_OBJECT_UNREF(args.extfftre);
    GWY_OBJECT_UNREF(args.extfftim);
    g_object_unref(args.params);
    g_object_unref(args.highpass);
    g_object_unref(args.lowpass);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *dataview;
    GwyDialogOutcome outcome;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    gui.vf = gwy_data_field_get_value_format_xy(args->field, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);

    gui.dialog = gwy_dialog_new(_("Frequency Split"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);
    /* FIXME: Reciprocal value is difficult to add as alternative slider because it goes to ∞.  Keep it as a silly
     * little label on the side. */
    gwy_param_table_append_slider(table, PARAM_CUTOFF);
    gwy_param_table_append_info(table, INFO_WAVELENGTH, _("Wavelength"));
    gwy_param_table_set_unitstr(table, INFO_WAVELENGTH, gui.vf->units);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_WIDTH);
    gwy_param_table_append_combo(table, PARAM_BOUNDARY);
    gwy_param_table_append_radio(table, PARAM_PREVIEW);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkboxes(table, PARAM_OUTPUT);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    gwy_si_unit_value_format_free(gui.vf);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    FSplitPreviewType display = gwy_params_get_enum(params, PARAM_PREVIEW);
    gboolean filter_changed = (id == PARAM_CUTOFF || id == PARAM_WIDTH);

    if (id < 0 || id == PARAM_CUTOFF)
        update_real_frequency(gui);

    if (id < 0 || id == PARAM_BOUNDARY || (display == FSPLIT_PREVIEW_ORIGINAL && filter_changed)) {
        /* Recalculate everything when
         * (a) We must because the boundary treatment has changed.
         * (b) We are currently recalculating nothing because original data are shown. */
        GWY_OBJECT_UNREF(args->extfftre);
        GWY_OBJECT_UNREF(args->extfftim);
    }

    if (id < 0 || id == PARAM_BOUNDARY || filter_changed)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));

    if (id < 0 || id == PARAM_PREVIEW) {
        GQuark quark = gwy_app_get_data_key_for_id(0);
        if (display == FSPLIT_PREVIEW_LOW_PASS)
            gwy_container_set_object(gui->data, quark, args->lowpass);
        else if (display == FSPLIT_PREVIEW_HIGH_PASS)
            gwy_container_set_object(gui->data, quark, args->highpass);
        else
            gwy_container_set_object(gui->data, quark, args->field);
    }
}

static void
update_real_frequency(ModuleGUI *gui)
{
    GwyParamTable *table = gui->table;
    GwySIValueFormat *vf = gui->vf;
    gdouble dx, v = gwy_params_get_double(gui->args->params, PARAM_CUTOFF);
    gchar *s;

    if (!v) {
        gwy_param_table_info_set_valuestr(table, INFO_WAVELENGTH, "∞");
        return;
    }
    dx = gwy_data_field_get_dx(gui->args->field);
    v = 2.0*dx/v;
    s = g_strdup_printf("%.*f", vf->precision+1, v/vf->magnitude);
    gwy_param_table_info_set_valuestr(table, INFO_WAVELENGTH, s);
    g_free(s);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    execute(args);
    gwy_data_field_data_changed(args->highpass);
    gwy_data_field_data_changed(args->lowpass);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
extend_one_row(const gdouble *row, guint n, gdouble *extrow, guint next)
{
    enum { SMEAR = 6 };
    gint k, i;
    gdouble der0, der1;

    g_return_if_fail(next < 3*n);
    gwy_assign(extrow, row, n);
    /* 0 and 1 in extension row coordinates, not primary row */
    der0 = (2*row[n-1] - row[n-2] - row[n-3])/3;
    der1 = (2*row[0] - row[1] - row[2])/3;
    k = next - n;
    for (i = 0; i < k; i++) {
        gdouble x, y, ww, w;

        y = w = 0.0;
        if (i < SMEAR) {
            ww = 2.0*(SMEAR-1 - i)/SMEAR;
            y += ww*(row[n-1] + der0*(i + 1));
            w += ww;
        }
        if (k-1 - i < SMEAR) {
            ww = 2.0*(i + SMEAR-1 - (k-1))/SMEAR;
            y += ww*(row[0] + der1*(k - i));
            w += ww;
        }
        if (i < n) {
            x = 1.0 - i/(k - 1.0);
            ww = x*x;
            y += ww*row[n-1 - i];
            w += ww;
        }
        if (k-1 - i < n) {
            x = 1.0 - (k-1 - i)/(k - 1.0);
            ww = x*x;
            y += ww*row[k-1 - i];
            w += ww;
        }
        extrow[n + i] = y/w;
    }
}

static GwyDataField*
extend_data_field_smconn(GwyDataField *field)
{
    GwyDataField *extfield, *flipped;
    gdouble dx = gwy_data_field_get_dx(field), dy = gwy_data_field_get_dy(field);
    gint xres, yres, extxres, extyres;
    const gdouble *data;
    gdouble *extdata, *buf;
    gint i, j;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    extxres = gwy_fft_find_nice_size(4*xres/3);
    extyres = gwy_fft_find_nice_size(4*yres/3);
    if (extxres >= 3*xres || extyres >= 3*extyres) {
        /* This is a silly case.  We just do not want to hit the assertion in extend_one_row(). */
        return gwy_data_field_extend(field, 0, 0, extxres - xres, extyres - yres,
                                     GWY_EXTERIOR_FIXED_VALUE, gwy_data_field_get_avg(field), FALSE);
    }

    extfield = gwy_data_field_new(extxres, extyres, extxres*dx, extyres*dy, FALSE);
    flipped = gwy_data_field_new(extyres, extxres, extyres*dx, extxres*dy, FALSE);
    data = gwy_data_field_get_data_const(field);

    /* Extend rows horizontally. */
    extdata = gwy_data_field_get_data(extfield);
    for (i = 0; i < yres; i++)
        extend_one_row(data + i*xres, xres, extdata + i*extxres, extxres);

    /* Extend columns, including the newly created ones. */
    gwy_data_field_flip_xy(extfield, flipped, FALSE);
    extdata = gwy_data_field_get_data(flipped);
    buf = g_new(gdouble, extyres);
    for (i = 0; i < extxres; i++) {
        extend_one_row(extdata + i*extyres, yres, buf, extyres);
        gwy_assign(extdata + i*extyres, buf, extyres);
    }

    /* Copy it back, extend the remaining rows and use the average to fill
     * the area unreachable by a single extension. */
    gwy_data_field_flip_xy(flipped, extfield, FALSE);
    g_object_unref(flipped);
    extdata = gwy_data_field_get_data(extfield);
    buf = g_renew(gdouble, buf, extxres);
    for (i = yres; i < extyres; i++) {
        extend_one_row(extdata + i*extxres, xres, buf, extxres);
        for (j = xres; j < extxres; j++)
            extdata[i*extxres + j] = 0.5*(extdata[i*extxres + j] + buf[j]);
    }

    return extfield;
}

static GwyDataField*
extend_data_field_mirror(GwyDataField *field)
{
    GwyDataField *extfield;
    gint extxres, extyres, xres, yres, i, j;
    gdouble dx = gwy_data_field_get_dx(field), dy = gwy_data_field_get_dy(field);
    const gdouble *data, *srow;
    gdouble *extdata, *trow;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    extxres = 2*xres;
    extyres = 2*yres;
    extfield = gwy_data_field_new(extxres, extyres, extxres*dx, extyres*dy, FALSE);
    data = gwy_data_field_get_data_const(field);
    extdata = gwy_data_field_get_data(extfield);

    for (i = 0; i < yres; i++) {
        srow = data + i*xres;
        trow = extdata + i*extxres;

        for (j = 0; j < xres; j++)
            trow[j] = trow[extxres-1 - j] = srow[j];

        srow = trow;
        trow = extdata + (extyres-1 - i)*extxres;
        gwy_assign(trow, srow, extxres);
    }

    return extfield;
}

static void
extend_and_fft(GwyDataField *field, GwyDataField **extfftre, GwyDataField **extfftim,
               gint *leftext, gint *topext, FSplitBoundaryType boundary)
{
    GwyDataField *extfield;
    gint xres, yres, xext, yext;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    *leftext = *topext = 0;
    if (boundary == FSPLIT_BOUNDARY_LAPLACE) {
        xext = gwy_fft_find_nice_size(5*xres/3);
        yext = gwy_fft_find_nice_size(5*yres/3);
        extfield = gwy_data_field_extend(field, xext/2, xext - xext/2, yext/2, yext - yext/2,
                                         GWY_EXTERIOR_LAPLACE, 0.0, FALSE);
        *leftext = xext/2;
        *topext = yext/2;
    }
    else if (boundary == FSPLIT_BOUNDARY_SMCONN) {
        /* The extension is asymmetrical, just to the right and bottom. */
        extfield = extend_data_field_smconn(field);
    }
    else if (boundary == FSPLIT_BOUNDARY_MIRROR) {
        /* The extension is asymmetrical, just to the right and bottom. */
        extfield = extend_data_field_mirror(field);
    }
    else {
        extfield = g_object_ref(field);
    }

    *extfftre = gwy_data_field_new_alike(extfield, FALSE);
    *extfftim = gwy_data_field_new_alike(extfield, FALSE);
    gwy_data_field_2dfft_raw(extfield, NULL, *extfftre, *extfftim, GWY_TRANSFORM_DIRECTION_FORWARD);
    g_object_unref(extfield);
}

static void
filter_frequencies(GwyDataField *refield, GwyDataField *imfield,
                   gdouble cutoff, gdouble width)
{
    gint xres = gwy_data_field_get_xres(refield);
    gint yres = gwy_data_field_get_yres(refield);
    gdouble *re = gwy_data_field_get_data(refield);
    gdouble *im = gwy_data_field_get_data(imfield);
    gint i, j;

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i,j) \
            shared(re,im,xres,yres,cutoff,width)
#endif
    for (i = 0; i < yres; i++) {
        gdouble fy = 2.0*MIN(i, yres-i)/yres;
        for (j = 0; j < xres; j++) {
            gdouble fx = 2.0*MIN(j, xres-j)/xres;
            gdouble q, f = sqrt(fx*fx + fy*fy);

            if (width > 0.0)
                q = 0.5*(erf((f - cutoff)/width) + 1.0);
            else
                q = (f >= cutoff ? 1.0 : 0.0);

            re[i*xres + j] *= q;
            im[i*xres + j] *= q;
        }
    }
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyDataField *field = args->field, *lowpass = args->lowpass, *highpass = args->highpass;
    GwyDataField *tmpre, *tmpim, *fre, *fim;
    FSplitBoundaryType boundary = gwy_params_get_enum(params, PARAM_BOUNDARY);
    gdouble cutoff = gwy_params_get_double(params, PARAM_CUTOFF);
    gdouble width = gwy_params_get_double(params, PARAM_WIDTH);
    gint xres, yres;

    if (!args->extfftre)
        extend_and_fft(args->field, &args->extfftre, &args->extfftim, &args->leftext, &args->topext, boundary);
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    tmpre = gwy_data_field_new_alike(args->extfftre, FALSE);
    tmpim = gwy_data_field_new_alike(args->extfftre, FALSE);
    fre = gwy_data_field_duplicate(args->extfftre);
    fim = gwy_data_field_duplicate(args->extfftim);
    filter_frequencies(fre, fim, cutoff, width);
    gwy_data_field_2dfft_raw(fre, fim, tmpre, tmpim, GWY_TRANSFORM_DIRECTION_BACKWARD);
    g_object_unref(tmpim);
    g_object_unref(fre);
    g_object_unref(fim);
    gwy_data_field_area_copy(tmpre, highpass, args->leftext, args->topext, xres, yres, 0, 0);
    g_object_unref(tmpre);
    gwy_data_field_subtract_fields(lowpass, field, highpass);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
