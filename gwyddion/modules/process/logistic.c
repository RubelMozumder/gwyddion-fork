/*
 *  $Id: logistic.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2016-2021 David Necas (Yeti), Petr Klapetek, Daniil Bratashov.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, dn2010@gwyddion.net
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
#include <glib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"
#include "preview.h"

#define RUN_MODES GWY_RUN_INTERACTIVE

#define FWHM2SIGMA (1.0/(2.0*sqrt(2*G_LN2)))

typedef enum {
    LOGISTIC_MODE_TRAIN,
    LOGISTIC_MODE_USE
} LogisticMode;

typedef enum {
    LOGISTIC_HESSIAN_DX2,
    LOGISTIC_HESSIAN_DY2,
    LOGISTIC_HESSIAN_DXDY,
} LogisticHessianFilter;

enum {
    PARAM_MODE,
    PARAM_USE_GAUSSIANS,
    PARAM_NGAUSSIANS,
    PARAM_USE_SOBEL,
    PARAM_USE_LAPLACIANS,
    PARAM_USE_HESSIAN,
    PARAM_LAMBDA,
};

typedef struct {
    GwyParams *params;
    GwyDataLine *thetas;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GwyParams *orig_params;
    GtkWidget *dialog;
    GwyParamTable *table;
    gboolean anything_has_changed;
} ModuleGUI;

static gboolean         module_register        (void);
static GwyParamDef*     define_module_params   (void);
static void             logistic               (GwyContainer *data,
                                                GwyRunType runtype);
static GwyDialogOutcome run_gui                (ModuleArgs *args);
static void             param_changed          (ModuleGUI *gui,
                                                gint id);
static GwyBrick*        create_feature_vector  (GwyDataField *field,
                                                ModuleArgs *args);
static gdouble          cost_function          (GwyBrick *brick,
                                                GwyDataField *mask,
                                                gdouble *thetas,
                                                gdouble *grad,
                                                gdouble lambda);
static void             train_logistic         (GwyContainer *container,
                                                gint id,
                                                GwyBrick *features,
                                                GwyDataField *mask,
                                                gdouble *thetas,
                                                gdouble lambda);
static void             predict_mask           (GwyBrick *brick,
                                                gdouble *thetas,
                                                GwyDataField *mask);
static void             logistic_filter_hessian(GwyDataField *field,
                                                LogisticHessianFilter filter_type);
static gint             logistic_nfeatures     (ModuleArgs *args);
static void             load_thetas            (ModuleArgs *args);
static void             save_thetas            (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Trains logistic regression to mark grains."),
    "Daniil Bratashov <dn2010@gwyddion.net>",
    "0.5",
    "David Nečas (Yeti) & Petr Klapetek & Daniil Bratashov",
    "2016",
};

GWY_MODULE_QUERY2(module_info, logistic)

static gboolean
module_register(void)
{
    gwy_process_func_register("logistic_regression",
                              (GwyProcessFunc)&logistic,
                              N_("/_Grains/Logistic _Regression..."),
                              GWY_STOCK_GRAINS,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Mark grains with logistic regression"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum modes[] = {
        { N_("_Use trained regression"),    LOGISTIC_MODE_USE,   },
        { N_("_Train logistic regression"), LOGISTIC_MODE_TRAIN, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "logistic");
    gwy_param_def_add_gwyenum(paramdef, PARAM_MODE, "mode", _("Mode"),
                              modes, G_N_ELEMENTS(modes), LOGISTIC_MODE_TRAIN);
    gwy_param_def_add_boolean(paramdef, PARAM_USE_GAUSSIANS, "usegaussians", _("_Gaussian blur"), TRUE);
    gwy_param_def_add_int(paramdef, PARAM_NGAUSSIANS, "ngaussians", _("_Number of Gaussians"), 1, 10, 4);
    gwy_param_def_add_boolean(paramdef, PARAM_USE_SOBEL, "usesobel", _("_Sobel derivatives"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_USE_LAPLACIANS, "uselaplacians", _("_Laplacian"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_USE_HESSIAN, "usehessian", _("_Hessian"), TRUE);
    gwy_param_def_add_double(paramdef, PARAM_LAMBDA, "lambda", _("_Regularization parameter"), 0.0, 10.0, 1.0);
    return paramdef;
}

static void
logistic(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *field, *mask;
    GwyDialogOutcome outcome;
    ModuleArgs args;
    GwyBrick *features;
    gint id, nfeatures;
    GQuark quark;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_MASK_FIELD, &mask,
                                     GWY_APP_MASK_FIELD_KEY, &quark,
                                     0);

    args.field = field;
    args.mask = mask;
    args.result = gwy_data_field_new_alike(field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result), NULL);
    args.params = gwy_params_new_from_settings(define_module_params());
    load_thetas(&args);

    outcome = run_gui(&args);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    nfeatures = logistic_nfeatures(&args);
    features = create_feature_vector(field, &args);

    if (gwy_params_get_enum(args.params, PARAM_MODE) == LOGISTIC_MODE_TRAIN) {
        /* XXX: We should not let the user think training is done when there is no mask. */
        if (mask) {
            gdouble lambda = gwy_params_get_double(args.params, PARAM_LAMBDA);

            gwy_data_line_resize(args.thetas, nfeatures, GWY_INTERPOLATION_NONE);
            train_logistic(data, id, features, mask, gwy_data_line_get_data(args.thetas), lambda);
            save_thetas(&args);
        }
    }
    else {
        gwy_app_undo_qcheckpointv(data, 1, &quark);
        predict_mask(features, gwy_data_line_get_data(args.thetas), args.result);
        gwy_container_set_object(data, quark, args.result);
        gwy_app_channel_log_add_proc(data, id, id);
    }
    g_object_unref(features);

end:
    g_object_unref(args.thetas);
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.orig_params = gwy_params_duplicate(args->params);

    gui.dialog = gwy_dialog_new(_("Logistic Regression"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_MODE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_header(table, -1, _("Features"));
    gwy_param_table_append_checkbox(table, PARAM_USE_GAUSSIANS);
    gwy_param_table_append_slider(table, PARAM_NGAUSSIANS);
    gwy_param_table_slider_set_mapping(table, PARAM_NGAUSSIANS, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_checkbox(table, PARAM_USE_SOBEL);
    gwy_param_table_append_checkbox(table, PARAM_USE_LAPLACIANS);
    gwy_param_table_append_checkbox(table, PARAM_USE_HESSIAN);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_LAMBDA);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.orig_params);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    static const gint checkboxes[] = { PARAM_USE_GAUSSIANS, PARAM_USE_SOBEL, PARAM_USE_LAPLACIANS, PARAM_USE_HESSIAN };

    GwyParams *params = gui->args->params, *orig_params = gui->orig_params;
    LogisticMode mode = gwy_params_get_enum(params, PARAM_MODE);
    GwyParamTable *table = gui->table;
    gboolean anything_has_changed = FALSE;
    guint i;

    for (i = 0; i < G_N_ELEMENTS(checkboxes); i++) {
        if (gwy_params_get_boolean(params, checkboxes[i]) != gwy_params_get_boolean(orig_params, checkboxes[i]))
            anything_has_changed = TRUE;
    }
    if (gwy_params_get_int(params, PARAM_NGAUSSIANS) != gwy_params_get_int(orig_params, PARAM_NGAUSSIANS))
        anything_has_changed = TRUE;
    if (anything_has_changed != gui->anything_has_changed) {
        gui->anything_has_changed = anything_has_changed;
        gwy_param_table_radio_set_sensitive(table, PARAM_MODE, LOGISTIC_MODE_USE, !anything_has_changed);
    }

    if (id < 0 || id == PARAM_MODE) {
        gboolean is_training = (mode == LOGISTIC_MODE_TRAIN);
        gwy_param_table_set_sensitive(table, PARAM_USE_GAUSSIANS, is_training);
        gwy_param_table_set_sensitive(table, PARAM_NGAUSSIANS, is_training);
        gwy_param_table_set_sensitive(table, PARAM_USE_SOBEL, is_training);
        gwy_param_table_set_sensitive(table, PARAM_USE_LAPLACIANS, is_training);
        gwy_param_table_set_sensitive(table, PARAM_USE_HESSIAN, is_training);
        gwy_param_table_set_sensitive(table, PARAM_LAMBDA, is_training);
    }
}

static void
assign_feature(GwyDataField *feature, GwyBrick *brick, gint *z)
{
    gwy_data_field_normalize(feature);
    gwy_data_field_add(feature, -gwy_data_field_get_avg(feature));
    gwy_brick_set_xy_plane(brick, feature, *z);
    (*z)++;
}

static void
assign_all_features(GwyDataField *feature, GwyDataField *feature0, GwyBrick *brick, gint *z,
                    gboolean use_laplacian, gboolean use_sobel, gboolean use_hessian)
{
    assign_feature(feature0, brick, z);

    if (use_laplacian) {
        gwy_data_field_copy(feature0, feature, FALSE);
        gwy_data_field_filter_laplacian(feature);
        assign_feature(feature, brick, z);
    }

    if (use_sobel) {
        gwy_data_field_copy(feature0, feature, FALSE);
        gwy_data_field_filter_sobel(feature, GWY_ORIENTATION_HORIZONTAL);
        assign_feature(feature, brick, z);

        gwy_data_field_copy(feature0, feature, FALSE);
        gwy_data_field_filter_sobel(feature, GWY_ORIENTATION_VERTICAL);
        assign_feature(feature, brick, z);
    }

    if (use_hessian) {
        gwy_data_field_copy(feature0, feature, FALSE);
        logistic_filter_hessian(feature, LOGISTIC_HESSIAN_DX2);
        assign_feature(feature, brick, z);

        gwy_data_field_copy(feature0, feature, FALSE);
        logistic_filter_hessian(feature, LOGISTIC_HESSIAN_DY2);
        assign_feature(feature, brick, z);

        gwy_data_field_copy(feature0, feature, FALSE);
        logistic_filter_hessian(feature, LOGISTIC_HESSIAN_DXDY);
        assign_feature(feature, brick, z);
    }
}

static GwyBrick*
create_feature_vector(GwyDataField *field, ModuleArgs *args)
{
    GwyBrick *features, *transposed;
    GwyDataField *feature0, *featureg, *feature;
    gdouble xreal, yreal, size;
    gint xres, yres, z, zres, i;
    gint ngauss = gwy_params_get_int(args->params, PARAM_NGAUSSIANS);
    gboolean use_gaussians = gwy_params_get_boolean(args->params, PARAM_USE_GAUSSIANS);
    gboolean use_laplacian = gwy_params_get_boolean(args->params, PARAM_USE_LAPLACIANS);
    gboolean use_sobel = gwy_params_get_boolean(args->params, PARAM_USE_SOBEL);
    gboolean use_hessian = gwy_params_get_boolean(args->params, PARAM_USE_HESSIAN);

    feature0 = gwy_data_field_duplicate(field);
    feature = gwy_data_field_new_alike(feature0, FALSE);
    featureg = gwy_data_field_new_alike(feature0, FALSE);
    xres = gwy_data_field_get_xres(feature0);
    yres = gwy_data_field_get_yres(feature0);
    xreal = gwy_data_field_get_xreal(feature0);
    yreal = gwy_data_field_get_yreal(feature0);
    if (!use_gaussians)
        ngauss = 0;
    zres = logistic_nfeatures(args);
    features = gwy_brick_new(xres, yres, zres, xreal, yreal, zres, TRUE);

    z = 0;
    assign_all_features(feature, feature0, features, &z, use_laplacian, use_sobel, use_hessian);
    for (i = 0, size = 2.0; i < ngauss; i++, size *= 2.0) {
        gwy_data_field_copy(feature0, featureg, FALSE);
        gwy_data_field_filter_gaussian(featureg, size * FWHM2SIGMA);
        assign_all_features(feature, featureg, features, &z, use_laplacian, use_sobel, use_hessian);
    }
    g_object_unref(featureg);
    g_object_unref(feature);
    g_object_unref(feature0);

    /* Put all feature values for one pixel into a contiguous block.  Does not matter for application but improves
     * memory access in training. */
    transposed = gwy_brick_new(1, 1, 1, 1.0, 1.0, 1.0, FALSE);
    gwy_brick_transpose(features, transposed, GWY_BRICK_TRANSPOSE_YZX, FALSE, FALSE, FALSE);
    g_object_unref(features);

    return transposed;
}

static inline gdouble
sigmoid(gdouble z)
{
    return 1.0/(1.0 + exp(-z));
}

static void
train_logistic(GwyContainer *container, gint id,
               GwyBrick *features, GwyDataField *mask, gdouble *thetas, gdouble lambda)
{
    gdouble *grad, *oldgrad;
    gdouble epsilon, alpha, sum;
    G_GNUC_UNUSED gdouble cost;
    gint i, iter, maxiter, fres;
    gboolean converged = FALSE, cancelled = FALSE;

    fres = gwy_brick_get_zres(features);
    grad = g_new0(gdouble, fres);
    oldgrad = g_new0(gdouble, fres);
    epsilon = 1E-5;
    alpha = 10.0;
    iter = 0;
    maxiter = 2000;
    gwy_app_wait_start(gwy_app_find_window_for_channel(container, id), _("Training..."));
    while (!converged && !cancelled) {
        if (!gwy_app_wait_set_fraction((gdouble)iter/maxiter)) {
            cancelled = TRUE;
            break;
        }
        cost = cost_function(features, mask, thetas, grad, lambda);

        sum = 0;
        for (i = 0; i < fres; i++)
            sum += grad[i]*oldgrad[i];

        if (sum > 0) {
            alpha *= 1.05;
        }
        else if (sum < 0) {
            for (i = 0; i < fres; i++)
                grad[i] += oldgrad[i];
            alpha /= 2.0;
        }

        converged = TRUE;
        for (i = 0; i < fres; i++) {
            thetas[i] -= alpha * grad[i];
            if (fabs(grad[i]) > epsilon)
                converged = FALSE;
            oldgrad[i] = grad[i];
        }

        if (iter >= maxiter)
            converged = TRUE;
        iter++;
    }
    gwy_app_wait_finish();
    g_free(grad);
    g_free(oldgrad);
}

static gdouble
cost_function(GwyBrick *brick, GwyDataField *mask,
              gdouble *thetas, gdouble *grad, gdouble lambda)
{
    gint i, j, m, xres, yres, fres;
    gdouble ksum, jsum;
    const gdouble *fdata, *mdata;

    fres = gwy_brick_get_xres(brick);
    xres = gwy_brick_get_yres(brick);
    yres = gwy_brick_get_zres(brick);
    fdata = gwy_brick_get_data_const(brick);
    g_assert(xres == gwy_data_field_get_xres(mask));
    g_assert(yres == gwy_data_field_get_yres(mask));
    m = xres*yres;
    mdata = gwy_data_field_get_data_const(mask);

    jsum = 0;
    gwy_clear(grad, fres);

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            reduction(+:jsum) \
            private(i,j) \
            shared(xres,yres,fres,m,fdata,mdata,thetas,grad)
#endif
    {
        gdouble *tgrad = gwy_omp_if_threads_new0(grad, fres);
        gint ifrom = gwy_omp_chunk_start(yres), ito = gwy_omp_chunk_end(yres);

        for (i = ifrom; i < ito; i++) {
            for (j = 0; j < xres; j++) {
                const gdouble *fblock = fdata + (i*xres + j)*fres;
                gdouble h, y = mdata[i*xres + j], sum = 0.0;
                gint k;

                for (k = 0; k < fres; k++)
                    sum += fblock[k] * thetas[k];
                h = sigmoid(sum);
                jsum += -log(h)*y - log(1-h)*(1-y);
                for (k = 0; k < fres; k++)
                    tgrad[k] += fblock[k] * (h - y)/m;
            }
        }
        gwy_omp_if_threads_sum_double(grad, tgrad, fres);
    }
    for (i = 1; i < fres; i++)
        grad[i] += thetas[i] * lambda/m;
    jsum /= m;

    ksum = 0.0;
    for (i = 1; i < fres; i++)
        ksum += thetas[i] * thetas[i];
    jsum += ksum * 0.5*lambda/m;

    return jsum;
}

static void
predict_mask(GwyBrick *brick, gdouble *thetas, GwyDataField *mask)
{
    gint i, j, xres, yres, fres;
    const gdouble *fdata;
    gdouble *mdata;

    fres = gwy_brick_get_xres(brick);
    xres = gwy_brick_get_yres(brick);
    yres = gwy_brick_get_zres(brick);
    fdata = gwy_brick_get_data_const(brick);
    mdata = gwy_data_field_get_data(mask);
    g_assert(xres == gwy_data_field_get_xres(mask));
    g_assert(yres == gwy_data_field_get_yres(mask));

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i,j) \
            shared(xres,yres,fres,fdata,mdata,thetas)
#endif
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            const gdouble *fblock = fdata + (i*xres + j)*fres;
            gdouble sum = 0.0;
            gint k;

            for (k = 0; k < fres; k++)
                sum += fblock[k]*thetas[k];
            mdata[i*xres + j] = (sigmoid(sum) > 0.5) ? 1.0 : 0.0;
        }
    }
}

static void
logistic_filter_hessian(GwyDataField *field, LogisticHessianFilter filter_type)
{
    static const gdouble dx2_kernel[] = {
        0.125, -0.25, 0.125,
        0.25,  -0.5,  0.25,
        0.125, -0.25, 0.125,
    };
    static const gdouble dy2_kernel[] = {
        0.125,  0.25, 0.125,
        -0.25,  -0.5, -0.25,
        0.125,  0.25, 0.125,
    };
    static const gdouble dxdy_kernel[] = {
        0.5,  0, -0.5,
        0,    0, 0,
        -0.5, 0, 0.5,
    };
    GwyDataField *kernel = gwy_data_field_new(3, 3, 3, 3, FALSE);
    gdouble *kdata = gwy_data_field_get_data(kernel);

    if (filter_type == LOGISTIC_HESSIAN_DX2)
        gwy_assign(kdata, dx2_kernel, 3*3);
    else if (filter_type == LOGISTIC_HESSIAN_DY2)
        gwy_assign(kdata, dy2_kernel, 3*3);
    else if (filter_type == LOGISTIC_HESSIAN_DXDY)
        gwy_assign(kdata, dxdy_kernel, 3*3);
    else {
        g_return_if_reached();
    }
    gwy_data_field_convolve(field, kernel);
    g_object_unref(kernel);
}

static gint
logistic_nfeatures(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gint ngauss, nfeatures;

    nfeatures = 1;

    if (gwy_params_get_boolean(params, PARAM_USE_GAUSSIANS))
        ngauss = gwy_params_get_int(params, PARAM_NGAUSSIANS);
    else
        ngauss = 0;
    nfeatures += ngauss;

    if (gwy_params_get_boolean(params, PARAM_USE_LAPLACIANS))
        nfeatures += ngauss + 1;
    if (gwy_params_get_boolean(params, PARAM_USE_SOBEL))
        nfeatures += 2 * (ngauss + 1);
    if (gwy_params_get_boolean(params, PARAM_USE_HESSIAN))
        nfeatures += 3 * (ngauss + 1);

    return nfeatures;
}

static void
load_thetas(ModuleArgs *args)
{
    gchar *buffer, *line, *p;
    gsize size;
    gint i, nfeatures;
    gdouble *thetas;

    nfeatures = logistic_nfeatures(args);
    args->thetas = gwy_data_line_new(nfeatures, nfeatures, TRUE);
    if (!gwy_module_data_load("logistic", "thetas", &buffer, &size, NULL))
        return;

    thetas = gwy_data_line_get_data(args->thetas);
    p = buffer;
    i = 0;
    while ((line = gwy_str_next_line(&p)) && i < nfeatures)
        thetas[i++] = g_ascii_strtod(line, NULL);
    g_free(buffer);
}

static void
save_thetas(ModuleArgs *args)
{
    gdouble *thetas;
    gint i, nfeatures;
    FILE *fh;

    thetas = gwy_data_line_get_data(args->thetas);
    if (!(fh = gwy_module_data_fopen("logistic", "thetas", "w", NULL)))
        return;

    nfeatures = logistic_nfeatures(args);
    for (i = 0; i < nfeatures; i++) {
        gchar s[G_ASCII_DTOSTR_BUF_SIZE];

        g_ascii_dtostr(s, G_ASCII_DTOSTR_BUF_SIZE, thetas[i]);
        fputs(s, fh);
        fputc('\n', fh);
    }
    fclose(fh);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
