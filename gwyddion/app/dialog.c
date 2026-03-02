/*
 *  $Id: dialog.c 25742 2023-09-29 10:25:14Z yeti-dn $
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
#include <stdarg.h>
#include "libgwyddion/gwymacros.h"
#include "libprocess/gwyprocesstypes.h"
#include "libprocess/gwyprocessenums.h"
#include "libgwymodule/gwymodule.h"
#include "help.h"
#include "wait.h"
#include "dialog.h"
#include "param-table.h"
#include "param-internal.h"

typedef struct _GwyDialogPrivate GwyDialogPrivate;

typedef struct {
    GwyParamTable *partable;
    GwyParamType expected_type;
    gint id;
} GwyDialogTrackedParam;

struct _GwyDialogPrivate {
    GPtrArray *tables;
    gint default_response;

    GwyPreviewType preview_style;
    GwyDialogPreviewFunc preview_func;
    gpointer preview_data;
    GDestroyNotify preview_destroy;
    gulong preview_sid;

    GwyDialogTrackedParam instant_updates;

    gint in_update;
    gboolean did_init : 1;
    gboolean initial_invalidate : 1;
    gboolean have_result : 1;
    gboolean have_preview_button : 1;
    gboolean consolidated_reset : 1;
    gboolean instant_updates_is_on : 1;
    gboolean update_pressed : 1;
};

static void     gwy_dialog_finalize              (GObject *gobject);
static void     gwy_dialog_destroy               (GtkObject *gtkobject);
static void     gwy_dialog_response              (GtkDialog *gtkdialog,
                                                  gint response);
static void     everything_has_changed           (GwyDialog *dialog);
static void     look_for_instant_updates_param   (GwyDialog *dialog,
                                                  GwyParamTable *partable);
static void     notify_tables_proceed            (GwyDialog *dialog);
static void     update_tracked_params            (GwyDialog *dialog);
static gboolean rebind_tracked_param             (GwyDialog *dialog,
                                                  GwyDialogTrackedParam *tp);
static void     update_preview_button_sensitivity(GwyDialog *dialog);
static void     handle_instant_updates_enabled   (GwyDialog *dialog);
static gboolean preview_gsource                  (gpointer user_data);
static void     preview_immediately              (GwyDialog *dialog);
static void     reset_all_parameters             (GwyDialog *dialog);

static guint param_table_param_changed = 0;

G_DEFINE_TYPE(GwyDialog, gwy_dialog, GTK_TYPE_DIALOG);

static void
gwy_dialog_class_init(GwyDialogClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GtkObjectClass *gtkobject_class = GTK_OBJECT_CLASS(klass);
    GtkDialogClass *dialog_class = GTK_DIALOG_CLASS(klass);

    gobject_class->finalize = gwy_dialog_finalize;
    gtkobject_class->destroy = gwy_dialog_destroy;
    dialog_class->response = gwy_dialog_response;

    g_type_class_add_private(klass, sizeof(GwyDialogPrivate));
}

static void
gwy_dialog_init(GwyDialog *dialog)
{
    GwyDialogPrivate *priv;

    dialog->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE(dialog, GWY_TYPE_DIALOG, GwyDialogPrivate);
    priv->tables = g_ptr_array_new_full(0, g_object_unref);
    priv->consolidated_reset = TRUE;
    priv->instant_updates.id = -1;
    priv->instant_updates.expected_type = GWY_PARAM_BOOLEAN;

    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
}

static void
gwy_dialog_finalize(GObject *gobject)
{
    GwyDialog *dialog = (GwyDialog*)gobject;
    GwyDialogPrivate *priv = dialog->priv;

    g_ptr_array_free(priv->tables, TRUE);

    G_OBJECT_CLASS(gwy_dialog_parent_class)->finalize(gobject);
}

static void
gwy_dialog_destroy(GtkObject *gtkobject)
{
    GwyDialog *dialog = (GwyDialog*)gtkobject;
    GwyDialogPrivate *priv = dialog->priv;

    if (priv->preview_sid) {
        gwy_debug("removing preview gsource %lu because we are being destroyed", priv->preview_sid);
        g_source_remove(priv->preview_sid);
        priv->preview_sid = 0;
    }

    if (priv->preview_destroy) {
        priv->preview_destroy(priv->preview_data);
        priv->preview_func = NULL;
        priv->preview_destroy = NULL;
    }

    GTK_OBJECT_CLASS(gwy_dialog_parent_class)->destroy(gtkobject);
}

/**
 * gwy_dialog_new:
 * @title: Title of the dialog window or %NULL.
 *
 * Creates a new data processing module dialog window.
 *
 * The dialog is modal by default.
 *
 * Returns: A new data processing module dialog window.
 *
 * Since: 2.59
 **/
GtkWidget*
gwy_dialog_new(const gchar *title)
{
    GtkWidget *dialog;

    dialog = g_object_new(GWY_TYPE_DIALOG, NULL);
    if (title)
        gtk_window_set_title(GTK_WINDOW(dialog), title);

    return dialog;
}

/**
 * gwy_dialog_add_buttons:
 * @dialog: A data processing module dialog window.
 * @response_id: Response identifier from either #GtkResponseType or #GwyResponseType enum.  Not all #GtkResponseType
 *               values may do something useful.
 * @...: List of more response identifiers, terminated by 0.
 *
 * Adds stock buttons to data processing module dialog window.
 *
 * Beside #GwyResponseType, the following GTK+ responses are recognised and handled automatically.
 *
 * %GTK_RESPONSE_OK or %GTK_RESPONSE_ACCEPT creates an OK button which finishes the dialog with result
 * %GWY_DIALOG_PROCEED (or %GWY_DIALOG_HAVE_RESULT if result has been calculated).
 *
 * %GTK_RESPONSE_CANCEL or %GTK_RESPONSE_REJECT creates a Cancel button which finishes the dialog with result
 * %GWY_DIALOG_CANCEL.
 *
 * Since: 2.59
 **/
void
gwy_dialog_add_buttons(GwyDialog *dialog,
                       gint response_id,
                       ...)
{
    GwyDialogPrivate *priv;
    GtkSettings *settings;
    GtkDialog *gtkdialog;
    gboolean buttons_have_images;
    va_list ap;
    gint respid;

    g_return_if_fail(GWY_IS_DIALOG(dialog));
    gtkdialog = GTK_DIALOG(dialog);
    priv = dialog->priv;
    settings = gtk_settings_get_default();
    g_object_get(settings, "gtk-button-images", &buttons_have_images, NULL);

    respid = response_id;
    va_start(ap, response_id);
    while (respid) {
        const gchar *button_text = NULL, *button_stock_id = NULL;

        if (respid == GTK_RESPONSE_OK || respid == GTK_RESPONSE_ACCEPT) {
            priv->default_response = respid;
            button_stock_id = GTK_STOCK_OK;
        }
        else if (respid == GTK_RESPONSE_CANCEL || respid == GTK_RESPONSE_REJECT)
            button_stock_id = GTK_STOCK_CANCEL;
        else if (respid == GWY_RESPONSE_CLEAR)
            button_stock_id = GTK_STOCK_CLEAR;
        else if (respid == GWY_RESPONSE_RESET)
            button_text = _("_Reset");
        else if (respid == GWY_RESPONSE_UPDATE) {
            button_stock_id = GTK_STOCK_EXECUTE;
            button_text = _("_Update");
            priv->have_preview_button = TRUE;
        }
        else {
            g_assert_not_reached();
            button_text = "???";
        }

        if (buttons_have_images && button_text && button_stock_id)
            gtk_dialog_add_action_widget(gtkdialog, gwy_stock_like_button_new(button_text, button_stock_id), respid);
        else if (button_stock_id)
            gtk_dialog_add_button(gtkdialog, button_stock_id, respid);
        else
            gtk_dialog_add_button(gtkdialog, button_text, respid);

        respid = va_arg(ap, gint);
    }
    va_end(ap);
}

/**
 * gwy_dialog_add_content:
 * @dialog: A data processing module dialog window.
 * @child: Child widget to add.
 * @expand: %TRUE if the @child is to be given extra space allocated to the content area.
 * @fill: %TRUE if space given by @expand option is actually allocated to @child, rather than just padding it
 * @padding: Extra space to put between @child and its neighbours, including outer edges.
 *
 * Adds a widget to the data processing module dialog window content area.
 *
 * By tradition, the dialog content is in a vertical #GtkBox.  Parameters @expand, @fill and @padding have the same
 * meaning as in gtk_box_pack_start().
 *
 * Since: 2.59
 **/
void
gwy_dialog_add_content(GwyDialog *dialog,
                       GtkWidget *child,
                       gboolean expand,
                       gboolean fill,
                       gint padding)
{
    GtkWidget *content_vbox;

    g_return_if_fail(GWY_IS_DIALOG(dialog));
    g_return_if_fail(GTK_IS_WIDGET(child));

    content_vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_pack_start(GTK_BOX(content_vbox), child, expand, fill, padding);
}

/**
 * gwy_dialog_add_param_table:
 * @dialog: A data processing module dialog window.
 * @partable: Parameter table to add.
 *
 * Registers a parameter table with a data processing module dialog window.
 *
 * This function does not pack the table's widget anywhere; it just registers the table with @dialog.  Pack the
 * table's widget obtained gwy_param_table_widget() to the dialog using gwy_dialog_add_content() or some other
 * container widget.
 *
 * All parameters in all parameter tables in a #GwyDialog must have unique ids.  This is because parameters are
 * commonly refered to only by the id.  When this condition is satisfied, different parameter tables can correspond to
 * different #GwyParamDef definition sets.  Normally, however, multiple tables provide controls for different subsets
 * of parameters defined by one #GwyParamDef.
 *
 * If multiple tables are added, parameters in any table can be changed inside a #GwyParamTable::param-changed
 * callback without triggering infinite recursion.  This is because #GwyDialog prevents recursion also during
 * cross-updates.
 *
 * Usually, you can just connect all #GwyParamTable::param-changed signals to the same handler and ignore which table
 * emitted the signal.  All the useful information is in the parameter id.
 *
 * The dialog consumes the floating reference of @partable.  Usually this has the desired effect that @dialog becomes
 * the only owner and the table is destroyed when @dialog finishes.  However, if you want to later use
 * gwy_dialog_remove_param_table() without destroying the table you need to explicitly add your own reference.
 *
 * Since: 2.59
 **/
void
gwy_dialog_add_param_table(GwyDialog *dialog,
                           GwyParamTable *partable)
{
    GwyDialogPrivate *priv;
    GPtrArray *tables;
    guint i, n;

    g_return_if_fail(GWY_IS_DIALOG(dialog));
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));

    if (!param_table_param_changed)
        param_table_param_changed = g_signal_lookup("param-changed", GWY_TYPE_PARAM_TABLE);

    priv = dialog->priv;
    tables = priv->tables;
    n = tables->len;
    for (i = 0; i < n; i++) {
        GwyParamTable *othertable = g_ptr_array_index(tables, i);
        if (othertable == partable) {
            g_warning("Parameter table is already present in dialog.");
            return;
        }
    }
    g_object_ref_sink(partable);
    g_ptr_array_add(tables, partable);
    for (i = 0; i < priv->in_update; i++)
        _gwy_param_table_in_update(partable, TRUE);
    _gwy_param_table_set_parent_dialog(partable, dialog);
    look_for_instant_updates_param(dialog, partable);
    update_tracked_params(dialog);
}

/**
 * gwy_dialog_remove_param_table:
 * @dialog: A data processing module dialog window.
 * @partable: Parameter table to remove.
 *
 * Removes a parameter table from a data processing module dialog window.
 *
 * Removal releases the reference @dialog holds on @partable.  If you have not added your own reference @partable will
 * be destroyed when it is removed.
 *
 * Since: 2.59
 **/
void
gwy_dialog_remove_param_table(GwyDialog *dialog,
                              GwyParamTable *partable)
{
    GwyDialogPrivate *priv;
    GPtrArray *tables;
    guint n, i, j;

    g_return_if_fail(GWY_IS_DIALOG(dialog));
    g_return_if_fail(GWY_IS_PARAM_TABLE(partable));

    priv = dialog->priv;
    tables = priv->tables;
    n = tables->len;
    for (i = 0; i < n; i++) {
        if (g_ptr_array_index(tables, i) == partable) {
            for (j = 0; j < priv->in_update; j++)
                _gwy_param_table_in_update(partable, FALSE);
            _gwy_param_table_set_parent_dialog(partable, NULL);
            /* This unrefs the table.  Do not do it again. */
            g_ptr_array_remove_index(tables, i);
            update_tracked_params(dialog);
            return;
        }
    }
    g_assert_not_reached();
}

/**
 * gwy_dialog_set_preview_func:
 * @dialog: A data processing module dialog window.
 * @prevtype: Preview style to use.
 * @preview: Function which performs the preview (or %NULL for %GWY_PREVIEW_NONE).
 * @user_data: User data to pass to @preview.
 * @destroy: Function to call to free @user_data (or %NULL).
 *
 * Sets the preview function for a data processing module dialog.
 *
 * The preview function is called automatically when the dialog receives %GWY_RESPONSE_UPDATE response and/or after
 * gwy_dialog_invalidate().  Use gwy_dialog_set_instant_updates_param() if you have a parameter controlling instant
 * updates.
 *
 * Since: 2.59
 **/
void
gwy_dialog_set_preview_func(GwyDialog *dialog,
                            GwyPreviewType prevtype,
                            GwyDialogPreviewFunc preview,
                            gpointer user_data,
                            GDestroyNotify destroy)
{
    GwyDialogPrivate *priv;

    g_return_if_fail(GWY_IS_DIALOG(dialog));
    if (!preview && prevtype != GWY_PREVIEW_NONE) {
        g_warning("If there is no preview function the preview type must be NONE.");
        prevtype = GWY_PREVIEW_NONE;
    }
    /* XXX: Is the other weird case, a preview function with GWY_PREVIEW_NONE style, reasonable?  I suppose it could
     * be allowed for some kind of manual previews?  In any case, having a function we never invoke is much better
     * than wanting to invoke a function we do not have. */

    priv = dialog->priv;
    if (priv->preview_destroy)
        priv->preview_destroy(priv->preview_data);

    priv->preview_style = prevtype;
    priv->preview_func = preview;
    priv->preview_data = user_data;
    priv->preview_destroy = destroy;
}

/**
 * gwy_dialog_set_instant_updates_param:
 * @dialog: A data processing module dialog window.
 * @id: Parameter identifier.
 *
 * Sets the id of instant updates parameter for a data processing module dialog window.
 *
 * The parameter must be a boolean.  When it is %TRUE previews are immediate and the dialog button corresponding to
 * %GWY_RESPONSE_UPDATE is insensitive.  When it is %FALSE previews are manual, on pressing the dialog button which
 * is now sensitive.
 *
 * Setting the instant updates parameter makes the sensitivity of update dialog button automatic.  If you need more
 * control over it do not use this function.
 *
 * Since: 2.59
 **/
void
gwy_dialog_set_instant_updates_param(GwyDialog *dialog,
                                     gint id)
{
    GwyDialogPrivate *priv;

    g_return_if_fail(GWY_IS_DIALOG(dialog));

    priv = dialog->priv;
    priv->instant_updates.id = id;
    rebind_tracked_param(dialog, &priv->instant_updates);
    update_preview_button_sensitivity(dialog);
}

/**
 * gwy_dialog_run:
 * @dialog: A data processing module dialog window.
 *
 * Runs a data processing module dialog window.
 *
 * This function does not return until the dialog finishes with a final outcome.  The final outcome is usually either
 * cancellation or an OK response, as indicated by the return value.  Other response types – preview, parameter reset
 * or help – are generally handled inside and do not cause this function to return.  Furthermore, the dialog is always
 * destroyed when the function returns.  These are the main differences from gtk_dialog_run().
 *
 * Returns: The final outcome of the dialog.
 *
 * Since: 2.59
 **/
GwyDialogOutcome
gwy_dialog_run(GwyDialog *dialog)
{
    GwyDialogPrivate *priv;
    GtkDialog *gtkdialog;
    GwyDialogOutcome result = GWY_DIALOG_CANCEL;
    gboolean finished = FALSE, do_destroy = TRUE;

    g_return_val_if_fail(GWY_IS_DIALOG(dialog), result);
    gtkdialog = GTK_DIALOG(dialog);
    priv = dialog->priv;

    if (!priv->did_init) {
        /* XXX: This is lame.  But probably works well enough… */
        if (gwy_process_func_current())
            gwy_help_add_to_proc_dialog(gtkdialog, GWY_HELP_DEFAULT);
        else if (gwy_file_func_current())
            gwy_help_add_to_file_dialog(gtkdialog, GWY_HELP_DEFAULT);
        else if (gwy_graph_func_current())
            gwy_help_add_to_graph_dialog(gtkdialog, GWY_HELP_DEFAULT);
        else if (gwy_volume_func_current())
            gwy_help_add_to_volume_dialog(gtkdialog, GWY_HELP_DEFAULT);
        else if (gwy_xyz_func_current())
            gwy_help_add_to_xyz_dialog(gtkdialog, GWY_HELP_DEFAULT);
        else if (gwy_curve_map_func_current())
            gwy_help_add_to_cmap_dialog(gtkdialog, GWY_HELP_DEFAULT);

        if (priv->default_response)
            gtk_dialog_set_default_response(gtkdialog, priv->default_response);

        /* This tells the param-changed handler to do the final update.  Since the initial parameter set should be
         * valid this usually means updating sensitivity. */
        everything_has_changed(dialog);

        priv->did_init = TRUE;
    }

    gtk_widget_show_all(GTK_WIDGET(dialog));
    gtk_window_present(GTK_WINDOW(dialog));
    priv->initial_invalidate = TRUE;
    gwy_dialog_invalidate(dialog);
    priv->initial_invalidate = FALSE;

    do {
        gint response = gtk_dialog_run(gtkdialog);

        if (response == GTK_RESPONSE_NONE) {
            finished = TRUE;
            do_destroy = FALSE;
        }
        else if (response == GTK_RESPONSE_DELETE_EVENT
                 || response == GTK_RESPONSE_CANCEL
                 || response == GTK_RESPONSE_REJECT)
            finished = TRUE;
        else if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_ACCEPT) {
            result = (priv->have_result ? GWY_DIALOG_HAVE_RESULT : GWY_DIALOG_PROCEED);
            notify_tables_proceed(dialog);
            finished = TRUE;
        }
        /* Assume we can only get GWY_RESPONSE_UPDATE when it makes sense (there is a preview, instant updates are
         * disabled, etc.). */
        else if (response == GWY_RESPONSE_UPDATE) {
            priv->update_pressed = TRUE;
            preview_immediately(dialog);
        }
        else if (response == GWY_RESPONSE_RESET) {
            /* Do not handle GWY_RESPONSE_RESET here.  It would be run after everything and that is too late.  The
             * order of execution is:
             * 1. "response" handler we could connect upon construction (without after), but do not currently,
             * 2. "response" handler the module connects (without after),
             * 3. class handler gtkdialog_class->response(),
             * 4. "response" handler the module connects with G_CONNECT_AFTER, and
             * 5. here.
             * Do the auto-reset in the class handler. Modules can then easily fix up the param tables either before
             * or after our handler (or both), as they see fit. */
        }
        else {
            /* The caller may also add its own uncommon response types, so do not assume we can handle everything. */
            gwy_debug("custom response %d left unhandeld", response);
        }
    } while (!finished);

    /* If the response was GTK_RESPONSE_NONE we may no longer exist.  So be careful with the object. */
    if (do_destroy)
        gtk_widget_destroy(GTK_WIDGET(dialog));

    return result;
}

/**
 * gwy_dialog_invalidate:
 * @dialog: A data processing module dialog window.
 *
 * Notifies a data processing module dialog preview is no longer valid.
 *
 * This function should be called from #GwyParamTable::param-changed when a parameter influencing the result has
 * changed.  This means most parameter changes.  Some parameter do require invalidation, for instance target graphs or
 * mask colours.
 *
 * The function can have multiple effects.  In all cases it resets the result state back to having no results
 * (nullifying any previous gwy_dialog_have_result()).  Furthermore, if the preview style is %GWY_PREVIEW_IMMEDIATE
 * and instant updates are enabled it queues up a preview recomputation.  The preview is added as an idle source to
 * the GTK+ main loop.  Therefore, gwy_dialog_invalidate() can be safely called multiple times in succession without
 * triggering multiple recomputations.
 *
 * Since: 2.59
 **/
void
gwy_dialog_invalidate(GwyDialog *dialog)
{
    GwyDialogPrivate *priv;

    g_return_if_fail(GWY_IS_DIALOG(dialog));
    priv = dialog->priv;
    priv->have_result = FALSE;
    gwy_debug("dialog invalidated");

    if (priv->preview_style == GWY_PREVIEW_IMMEDIATE) {
        GwyDialogTrackedParam *tp = &priv->instant_updates;
        gboolean instant_updates = TRUE;

        if (!priv->initial_invalidate && tp->partable)
            instant_updates = gwy_params_get_boolean(gwy_param_table_params(tp->partable), tp->id);
        if (instant_updates && !priv->preview_sid) {
            priv->preview_sid = g_idle_add_full(G_PRIORITY_LOW, preview_gsource, dialog, NULL);
            gwy_debug("added preview gsource %lu after invalidation", priv->preview_sid);
        }
    }
}

/**
 * gwy_dialog_have_result:
 * @dialog: A data processing module dialog window.
 *
 * Notifies a data processing module dialog preview that results are available.
 *
 * Modules may not have any other means of preview than the full computation of the final result.  It is then useful
 * to keep track whether it has been already computed to prevent unnecessary work.
 *
 * Calling this function makes gwy_dialog_run() return %GWY_DIALOG_HAVE_RESULT instead of the usual OK response
 * %GWY_DIALOG_PROCEED.  This indicates the result do not need to be recalculated (presumably you stored them in
 * a safe place).  The state is reset back to no results by gwy_dialog_invalidate().
 *
 * Since: 2.59
 **/
void
gwy_dialog_have_result(GwyDialog *dialog)
{
    g_return_if_fail(GWY_IS_DIALOG(dialog));
    dialog->priv->have_result = TRUE;
}

/**
 * gwy_dialog_update_requested:
 * @dialog: A data processing module dialog window.
 *
 * Reports whether preview was explicitly requested by the user.
 *
 * If the dialog has both immediate (partial) previews and an Update button to run a slower computation, the preview
 * function usually needs to distinguish between invocations from invalidation and pressing Update button. It can be
 * done using this function. If the preview is invoked by user pressing button with %GWY_RESPONSE_UPDATE response id
 * then it returns %TRUE, otherwise %FALSE.
 *
 * Returns: %TRUE if the Update button has been pressed since the last preview; %FALSE if it has not been pressed.
 *
 * Since: 2.64
 **/
gboolean
gwy_dialog_update_requested(GwyDialog *dialog)
{
    g_return_val_if_fail(GWY_IS_DIALOG(dialog), FALSE);
    return dialog->priv->update_pressed;
}

void
_gwy_dialog_param_table_update_started(GwyDialog *dialog)
{
    GwyDialogPrivate *priv = dialog->priv;
    GPtrArray *tables = priv->tables;
    guint i, n = tables->len;

    /* NB: Here it is too late to attempt check parameter values before the update because they generally already have
     * the new values.  We can remember them at the end of update_finished. */

    dialog->priv->in_update++;
    for (i = 0; i < n; i++)
        _gwy_param_table_in_update((GwyParamTable*)g_ptr_array_index(tables, i), TRUE);
}

void
_gwy_dialog_param_table_update_finished(GwyDialog *dialog)
{
    GPtrArray *tables = dialog->priv->tables;
    guint i, n = tables->len;

    for (i = 0; i < n; i++)
        _gwy_param_table_in_update((GwyParamTable*)g_ptr_array_index(tables, i), FALSE);

    /* XXX: Here is the right place for check the values of tracked parameters and our own update. */
    update_preview_button_sensitivity(dialog);
    handle_instant_updates_enabled(dialog);
    dialog->priv->in_update--;
}

static void
everything_has_changed(GwyDialog *dialog)
{
    GPtrArray *tables = dialog->priv->tables;
    guint i, n;

    _gwy_dialog_param_table_update_started(dialog);
    /* TODO: there is currently no way to control consolidated reset. */
    n = (dialog->priv->consolidated_reset ? MIN(tables->len, 1) : tables->len);
    for (i = 0; i < n; i++)
        g_signal_emit(g_ptr_array_index(tables, i), param_table_param_changed, 0, -1);
    _gwy_dialog_param_table_update_finished(dialog);
}

static void
look_for_instant_updates_param(GwyDialog *dialog, GwyParamTable *partable)
{
    const GwyParamDefItem *def;
    GwyParamDef *pardef;
    guint i, n;

    pardef = gwy_params_get_def(gwy_param_table_params(partable));
    n = _gwy_param_def_size(pardef);
    for (i = 0; i < n; i++) {
        def = _gwy_param_def_item(pardef, i);
        if (def->type == GWY_PARAM_BOOLEAN && def->def.b.is_instant_updates) {
            gwy_debug("Found instant updates param %d in table %p", def->id, partable);
            gwy_dialog_set_instant_updates_param(dialog, def->id);
            return;
        }
    }
}

static void
notify_tables_proceed(GwyDialog *dialog)
{
    GPtrArray *tables = dialog->priv->tables;
    guint i, n = tables->len;

    for (i = 0; i < n; i++)
        _gwy_param_table_proceed(g_ptr_array_index(tables, i));
}

static void
update_tracked_params(GwyDialog *dialog)
{
    GwyDialogPrivate *priv = dialog->priv;

    if (rebind_tracked_param(dialog, &priv->instant_updates))
        update_preview_button_sensitivity(dialog);
}

static gboolean
rebind_tracked_param(GwyDialog *dialog, GwyDialogTrackedParam *tp)
{
    GwyDialogPrivate *priv = dialog->priv;
    GPtrArray *tables = priv->tables;
    guint i, n = tables->len;

    if (tp->id < 0) {
        priv->instant_updates_is_on = FALSE;
        if (tp->partable) {
            tp->partable = NULL;
            return TRUE;
        }
        return FALSE;
    }

    for (i = 0; i < n; i++) {
        GwyParamTable *partable = g_ptr_array_index(tables, i);
        const GwyParamDefItem *def;
        GwyParams *params;
        GwyParamDef *pardef;

        /* If the parameter's table is still among our tables then we are done. */
        if (partable == tp->partable)
            return FALSE;

        params = gwy_param_table_params(partable);
        pardef = gwy_params_get_def(params);
        def = _gwy_param_def_item(pardef, _gwy_param_def_index(pardef, tp->id));
        if (def) {
            if (def->type == tp->expected_type) {
                priv->instant_updates_is_on = gwy_params_get_boolean(params, tp->id);
                tp->partable = partable;
                return TRUE;
            }
            else
                g_warning("Expected type %d for tracked parameter, but found %d.", tp->expected_type, def->type);
        }
    }
    if (tp->partable) {
        priv->instant_updates_is_on = FALSE;
        tp->partable = NULL;
        return TRUE;
    }
    return FALSE;
}

static void
update_preview_button_sensitivity(GwyDialog *dialog)
{
    GwyDialogPrivate *priv = dialog->priv;
    GwyDialogTrackedParam *tp = &priv->instant_updates;
    gboolean instant_updates;

    if (!priv->have_preview_button || !tp->partable)
        return;

    instant_updates = gwy_params_get_boolean(gwy_param_table_params(tp->partable), tp->id);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GWY_RESPONSE_UPDATE, !instant_updates);
}

static void
handle_instant_updates_enabled(GwyDialog *dialog)
{
    GwyDialogPrivate *priv = dialog->priv;
    GwyDialogTrackedParam *tp = &priv->instant_updates;
    gboolean instant_updates_was_on = priv->instant_updates_is_on;

    if (!tp->partable)
        return;

    priv->instant_updates_is_on = gwy_params_get_boolean(gwy_param_table_params(tp->partable), tp->id);
    gwy_debug("partable %p, was on %d, is on now %d, have result %d",
              tp->partable, instant_updates_was_on, priv->instant_updates_is_on, priv->have_result);
    /* Queue a preview after switching instant updates on, but only the preview is not valid.
     * This allows the module to avoid entirely calling gwy_dialog_invalidate() when instant updates option change.
     * Which is good because the correct logic is convoluted. */
    if (!tp->partable || instant_updates_was_on || !priv->instant_updates_is_on || priv->have_result)
        return;

    gwy_debug("invalidating because instant updates were switched on");
    gwy_dialog_invalidate(dialog);
}

static gboolean
preview_gsource(gpointer user_data)
{
    GwyDialog *dialog = (GwyDialog*)user_data;

    /* Do we it before or after? */
    gwy_debug("clearing preview gsource %lu and running the preview", dialog->priv->preview_sid);
    dialog->priv->preview_sid = 0;
    preview_immediately(dialog);

    return FALSE;
}

static void
preview_immediately(GwyDialog *dialog)
{
    GwyDialogPrivate *priv = dialog->priv;
    GwyPreviewType preview_style = priv->preview_style;
    GwyDialogTrackedParam *tp = &priv->instant_updates;
    gboolean change_cursor;

    if (!priv->preview_func)
        return;

    change_cursor = (preview_style == GWY_PREVIEW_UPON_REQUEST
                     || (preview_style == GWY_PREVIEW_IMMEDIATE
                         && tp->partable
                         && !gwy_params_get_boolean(gwy_param_table_params(tp->partable), tp->id)));

    if (change_cursor)
        gwy_app_wait_cursor_start(GTK_WINDOW(dialog));
    gwy_debug("calling preview_func() for preview");
    priv->preview_func(priv->preview_data);
    priv->update_pressed = FALSE;
    gwy_debug("preview_func() finished");
    if (change_cursor)
        gwy_app_wait_cursor_finish(GTK_WINDOW(dialog));
}

static void
gwy_dialog_response(GtkDialog *gtkdialog, gint response)
{
    GwyDialog *dialog = GWY_DIALOG(gtkdialog);

    gwy_debug("\"response\" class handler\n");
    if (response == GWY_RESPONSE_RESET)
        reset_all_parameters(dialog);
}

static void
reset_all_parameters(GwyDialog *dialog)
{
    GPtrArray *tables = dialog->priv->tables;
    guint i, n = tables->len;

    /* Reset controls, but do not emit any signals. */
    _gwy_dialog_param_table_update_started(dialog);
    for (i = 0; i < n; i++)
        gwy_param_table_reset((GwyParamTable*)g_ptr_array_index(tables, i));
    _gwy_dialog_param_table_update_finished(dialog);

    /* This is where we actually emit signals. */
    everything_has_changed(dialog);
}

/**
 * SECTION:dialog
 * @title: GwyDialog
 * @short_description: Data processing module dialog
 *
 * #GwyDialog is a #GtkDialog suitable for most data processing modules (including volume data, graph, etc.).
 * It offers simplified construction functions gwy_dialog_new(), gwy_dialog_add_buttons() and gwy_dialog_add_content().
 *
 * The main feature is integration with #GwyParamTable.  Parameter tables are registered using
 * gwy_dialog_add_param_table().  The dialog can then perform some simple tasks itself.  For instance with
 * gwy_dialog_set_instant_updates_param() the sensitivity of an Update button is handled automatically together with
 * a part of the preview redrawing logic if you set up a preview function with gwy_dialog_set_preview_func().
 *
 * #GwyDialog also handles the state change logic typical for Gwyddion data processing modules.  The function
 * gwy_dialog_run() only exits when the dialog finishes with some kind of conclusion what to do next, see
 * #GwyDialogOutcome.  Other response can either be handled completely automatically, for instance %GWY_RESPONSE_RESET
 * and %GWY_RESPONSE_UPDATE, or in cooperation with the module which connects to the GtkDialog::response signal.
 *
 * If you need to prepare the parameter tables before the automatic reset, connect to the signal normally and it will
 * be run before the #GwyDialog handler. If you need to touch up the tables afterwards connect your handler with
 * %G_CONNECT_AFTER.
 **/

/**
 * GwyDialog:
 *
 * Object representing a set of parameter definitions.
 *
 * The #GwyDialog struct contains no public fields.
 *
 * Since: 2.59
 **/

/**
 * GwyDialogClass:
 *
 * Class of parameter definition sets.
 *
 * Since: 2.59
 **/

/**
 * GwyDialogOutcome:
 * @GWY_DIALOG_CANCEL: The dialog was cancelled or destroyed.  The module should save changed parameters but no data
 *                     should be modified.
 * @GWY_DIALOG_PROCEED: The computation should proceed.  Normally this means the user pressed the OK button.  If the
 *                      results are already available %GWY_DIALOG_HAVE_RESULT is used instead.
 * @GWY_DIALOG_HAVE_RESULT: The computation has already been done.  This response is used when
 *                          gwy_dialog_have_result() has been called since the last gwy_dialog_invalidate().
 *
 * Type of conclusion from a data processing module dialog.
 *
 * Since: 2.59
 **/

/**
 * GwyPreviewType:
 * @GWY_PREVIEW_NONE: There is no preview.  This is the default if you do not set any preview function.  It does not
 *                    make much sense to use it when setting up a preview function.
 * @GWY_PREVIEW_IMMEDIATE: Preview occurs immediately, either unconditionally or controlled by an instant updates
 *                         checkbox.  Dialog invalidation queues a preview if instant updates are not disabled.
 * @GWY_PREVIEW_UPON_REQUEST: Preview is only upon request, usually by pressing a button with response
 *                            %GWY_RESPONSE_UPDATE.  Dialog invalidation does not queue a preview.
 *
 * Style of preview in a data processing dialog.
 *
 * Since: 2.59
 **/

/**
 * GwyResponseType:
 * @GWY_RESPONSE_RESET: Reset of all parameters (that do not have the no-reset flag set).  Adding it to a #GwyDialog
 *                      creates a Reset button which is normally handled fully by the dialog itself.
 * @GWY_RESPONSE_UPDATE: Update of the preview.  Adding it to a #GwyDialog creates an Update button which is normally
 *                       handled fully by the dialog itself (including sensitivity tied to an instant updates
 *                       parameter).
 * @GWY_RESPONSE_CLEAR: Clearing/resetting of selection.  Adding it to a #GwyDialog creates a Clear button.  You need
 *                      to connect to #GwyDialog::response and handle the response yourself.
 *
 * Type of predefined dialog response types.
 *
 * Since: 2.59
 **/

/**
 * GwyDialogPreviewFunc:
 * @user_data: User data to passed to gwy_dialog_set_preview_func().
 *
 * Protoype of #GwyDialog preview functions.
 *
 * Since: 2.59
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
