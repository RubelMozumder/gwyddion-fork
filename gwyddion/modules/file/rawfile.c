/*
 *  $Id: rawfile.c 25862 2023-10-15 14:28:32Z yeti-dn $
 *  Copyright (C) 2003-2022 David Necas (Yeti), Petr Klapetek.
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

/**
 * [FILE-MAGIC-USERGUIDE]
 * Raw text files
 * any
 * Read
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Raw binary files
 * any
 * Read
 **/

/**
 * [FILE-MAGIC-MISSING]
 * Raw data, no particular format.
 **/

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwymodule/gwymodule-file.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwyinventorystore.h>
#include <app/gwymoduleutils.h>
#include <app/gwymoduleutils-file.h>
#include <app/gwyapp.h>
#include "../process/preview.h"

#include "err.h"

#define EPS 1e-6

#ifndef NAN
#define NAN (0.0/0.0)
#endif

/* Special-cased text data delimiters */
typedef enum {
    RAW_DELIM_WHITESPACE = -1,
    RAW_DELIM_OTHER      = -2,
    RAW_DELIM_TAB        =  9,
} RawFileDelimiter;

/* Preset table columns */
enum {
    COLUMN_NAME = 0,
    COLUMN_TYPE,
    COLUMN_SIZE,
    COLUMN_INFO,
};

enum {
    PRESET_BUTTON_LOAD,
    PRESET_BUTTON_STORE,
    PRESET_BUTTON_RENAME,
    PRESET_BUTTON_DELETE,
    PRESET_NBUTTONS,
};

enum {
    RAWFILE_ERROR_TOO_SHORT,
    RAWFILE_ERROR_TEXT_PARSING,
};

enum {
    /* Preset */
    PARAM_XRES,
    PARAM_YRES,
    PARAM_XREAL,
    PARAM_YREAL,
    PARAM_XYUNIT,
    PARAM_ZUNIT,
    PARAM_ZSCALE,
    PARAM_HAVE_MISSING,
    PARAM_MISSING_VALUE,
    PARAM_XYEXPONENT,
    PARAM_ZEXPONENT,
    PARAM_FORMAT,
    PARAM_LINEOFFSET,
    PARAM_BYTESWAP,
    PARAM_SKIPFIELDS,
    PARAM_DELIMITER,
    PARAM_DECOMMA,
    PARAM_BUILTIN,
    PARAM_OFFSET,
    PARAM_SIZE,
    PARAM_SKIP,
    PARAM_ROWSKIP,
    PARAM_REVBYTE,
    PARAM_REVSAMPLE,
    PARAM_SIGN,

    /* Module */
    PARAM_SQUARE_IMAGE,
    PARAM_SQUARE_PIXELS,
    PARAM_DELIMITER_TYPE,
    PARAM_PRESET,
    PARAM_TAKE_OVER,

    /* GUI */
    INFO_FILE,
};

/* Text or binary data? */
typedef enum {
    RAW_BINARY = 0,
    RAW_TEXT   = 1,
} RawFileFormat;

/* Predefined common binary formats */
typedef enum {
    RAW_NONE = 0,
    RAW_SIGNED_BYTE,
    RAW_UNSIGNED_BYTE,
    RAW_SIGNED_WORD16,
    RAW_UNSIGNED_WORD16,
    RAW_SIGNED_WORD32,
    RAW_UNSIGNED_WORD32,
    RAW_IEEE_FLOAT,
    RAW_IEEE_DOUBLE,
    RAW_SIGNED_WORD64,
    RAW_UNSIGNED_WORD64,
    RAW_IEEE_HALF,
    RAW_PASCAL_REAL,
    RAW_NTYPES
} RawFileBuiltin;

typedef gdouble (*RawStrtodFunc)(const gchar *nptr,
                                 const gchar *missingval,
                                 gchar **endptr);

typedef struct {
    const gchar *filename;
    guint filesize;
    guchar *buffer;
} RawFileFile;

typedef struct {
    GwyParams *params;
    RawFileFile file;
    GwyDataField *field;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GwyContainer *data;
    GtkWidget *dialog;
    GwyParamTable *table_dims;
    GwyParamTable *table_format;
    GtkWidget *dataview;
    GtkWidget *error;
    GtkWidget *presetlist;
    GtkWidget *presetname;
    GtkWidget *preset_buttons[PRESET_NBUTTONS];
    GtkWidget *preset_error;
} ModuleGUI;

static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static GwyParamDef*     define_preset_params    (void);
static void             add_preset_params       (GwyParamDef *paramdef);
static gint             rawfile_detect          (void);
static GwyContainer*    rawfile_load            (const gchar *filename,
                                                 GwyRunType runtype,
                                                 GError **error);
static GwyDialogOutcome run_gui                 (ModuleArgs *args);
static GtkWidget*       preview_box_new         (ModuleGUI *gui);
static GtkWidget*       dimensions_tab_new      (ModuleGUI *gui);
static GtkWidget*       format_tab_new          (ModuleGUI *gui);
static GtkWidget*       preset_tab_new          (ModuleGUI *gui);
static void             param_changed           (ModuleGUI *gui,
                                                 gint id);
static void             preview                 (gpointer user_data);
static GwyDataField*    read_data_field         (ModuleArgs *args,
                                                 GError **error);
static void             preset_selected         (ModuleGUI *gui);
static void             load_preset             (ModuleGUI *gui);
static void             save_preset             (ModuleGUI *gui);
static void             rename_preset           (ModuleGUI *gui);
static void             delete_preset           (ModuleGUI *gui);
static gboolean         preset_validate_name    (ModuleGUI *gui,
                                                 const gchar *name,
                                                 gboolean show_warning);
static void             rawfile_read_builtin    (ModuleArgs *args,
                                                 gdouble *data);
static void             rawfile_read_bits       (ModuleArgs *args,
                                                 gdouble *data);
static gboolean         rawfile_read_ascii      (ModuleArgs *args,
                                                 gdouble *data,
                                                 GError **error);
static gdouble          gwy_ascii_strtod        (const gchar *nptr,
                                                 const gchar *missingval,
                                                 gchar **endptr);
static gdouble          gwy_comma_strtod        (const gchar *nptr,
                                                 const gchar *missingval,
                                                 gchar **endptr);
static void             sanitise_params         (GwyParams *params,
                                                 gboolean full_module);
static RawFileDelimiter delimiter_string_to_type(const gchar *delim);

static GType preset_resource_type = 0;
static GQuark error_domain = 0;

/* Cached locale data. */
static const gchar *decimal_point = ".";
static guint decimal_point_len = 1;

static const GwyEnum builtins[] = {
    { N_("User-specified"),       RAW_NONE            },
    { N_("Signed byte"),          RAW_SIGNED_BYTE     },
    { N_("Unsigned byte"),        RAW_UNSIGNED_BYTE   },
    { N_("Signed 16bit word"),    RAW_SIGNED_WORD16   },
    { N_("Unsigned 16bit word"),  RAW_UNSIGNED_WORD16 },
    { N_("Signed 32bit word"),    RAW_SIGNED_WORD32   },
    { N_("Unsigned 32bit word"),  RAW_UNSIGNED_WORD32 },
    { N_("Signed 64bit word"),    RAW_SIGNED_WORD64   },
    { N_("Unsigned 64bit word"),  RAW_UNSIGNED_WORD64 },
    { N_("IEEE single"),          RAW_IEEE_FLOAT      },
    { N_("IEEE double"),          RAW_IEEE_DOUBLE     },
    { N_("IEEE half"),            RAW_IEEE_HALF       },
    { N_("Pascal real"),          RAW_PASCAL_REAL     },
};

static const guint builtin_size[RAW_NTYPES] = {
    0, 8, 8, 16, 16, 32, 32, 32, 64, 64, 64, 16, 48,
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports raw data files, both ASCII and binary, according to user-specified format."),
    "Yeti <yeti@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

GWY_MODULE_QUERY2(module_info, rawfile)

static gboolean
module_register(void)
{
    error_domain = g_quark_from_static_string("RAWFILE_ERROR");
    if (!preset_resource_type) {
        preset_resource_type = gwy_param_def_make_resource_type(define_preset_params(), "GwyRawFilePreset", NULL);
        gwy_resource_class_load(g_type_class_peek(preset_resource_type));
    }

    gwy_file_func_register("rawfile",
                           N_("Raw data files"),
                           (GwyFileDetectFunc)&rawfile_detect,
                           (GwyFileLoadFunc)&rawfile_load,
                           NULL,
                           NULL);
    /* We provide a detection function, but that's only to become the fallback loading method. */
    gwy_file_func_set_is_detectable("rawfile", FALSE);

    return TRUE;
}

static GwyParamDef*
define_preset_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "rawfile");
    add_preset_params(paramdef);

    return paramdef;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum delimiters[] = {
        { N_("Any whitespace"),  RAW_DELIM_WHITESPACE, },
        { N_("TAB character"),   RAW_DELIM_TAB,        },
        { N_("Other character"), RAW_DELIM_OTHER,      },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "rawfile");
    add_preset_params(paramdef);
    /* Derived parameters. */
    gwy_param_def_add_boolean(paramdef, PARAM_SQUARE_IMAGE, "xyreseq", _("S_quare image"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_SQUARE_PIXELS, "xymeasureeq", _("_Square pixels"), TRUE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DELIMITER_TYPE, NULL, _("_Field delimiter"),
                              delimiters, G_N_ELEMENTS(delimiters), RAW_DELIM_WHITESPACE);
    /* Module-level weird shit. */
    gwy_param_def_add_resource(paramdef, PARAM_PRESET, "preset", NULL,
                               gwy_resource_class_get_inventory(g_type_class_peek(preset_resource_type)), "");
    gwy_param_def_add_boolean(paramdef, PARAM_TAKE_OVER, "takeover",
                              _("_Automatically offer raw data import of unknown files"), FALSE);

    return paramdef;
}

static void
add_preset_params(GwyParamDef *paramdef)
{
    static const GwyEnum formats[] = {
        { N_("_Text data"),   RAW_TEXT,   },
        { N_("_Binary data"), RAW_BINARY, },
    };

    /* Dimensions & scales. */
    gwy_param_def_add_int(paramdef, PARAM_XRES, "xres", _("_Horizontal size"), 1, 16384, 500);
    gwy_param_def_add_int(paramdef, PARAM_YRES, "yres", _("_Vertical size"), 1, 16384, 500);
    gwy_param_def_add_double(paramdef, PARAM_XREAL, "xreal", _("_Width"), 1e-3, 1e4, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_YREAL, "yreal", _("_Height"), 1e-3, 1e4, 1.0);
    gwy_param_def_add_unit(paramdef, PARAM_XYUNIT, "xyunit", _("_Dimensions unit"), "m");
    gwy_param_def_add_unit(paramdef, PARAM_ZUNIT, "zunit", _("_Value unit"), "m");
    gwy_param_def_add_double(paramdef, PARAM_ZSCALE, "zscale", _("_Z-scale (per sample unit)"), 1e-3, 1e4, 1.0);
    gwy_param_def_add_boolean(paramdef, PARAM_HAVE_MISSING, "havemissing", _("Missin_g value substitute"), FALSE);
    gwy_param_def_add_string(paramdef, PARAM_MISSING_VALUE, "missingvalue", _("Missin_g value substitute"),
                             GWY_PARAM_STRING_NULL_IS_EMPTY, NULL, "-32768.0");
    /* Legacy parameters. We read them from presets and interpret them, but always write files with exponents equal to
     * zero and unit being equal to the actual unit with prefixes. The latter is true also for the old presets. The
     * same information is there confusingly twice. */
    gwy_param_def_add_int(paramdef, PARAM_XYEXPONENT, "xyexponent", NULL, -120, 120, 0);
    gwy_param_def_add_int(paramdef, PARAM_ZEXPONENT, "zexponent", NULL, -120, 120, 0);

    gwy_param_def_add_gwyenum(paramdef, PARAM_FORMAT, "format", NULL, formats, G_N_ELEMENTS(formats), RAW_BINARY);

    /* Text. */
    gwy_param_def_add_int(paramdef, PARAM_LINEOFFSET, "lineoffset", _("Start from _line"), 0, 1 << 28, 0);
    gwy_param_def_add_int(paramdef, PARAM_SKIPFIELDS, "skipfields", _("_Each row skip"), 0, 1 << 28, 0);
    gwy_param_def_add_string(paramdef, PARAM_DELIMITER, "delimiter", _("_Other delimiter"),
                             GWY_PARAM_STRING_NULL_IS_EMPTY | GWY_PARAM_STRING_DO_NOT_STRIP, NULL, "");
    gwy_param_def_add_boolean(paramdef, PARAM_DECOMMA, "decomma", _("_Decimal separator is comma"), FALSE);

    /* Binary */
    gwy_param_def_add_gwyenum(paramdef, PARAM_BUILTIN, "builtin", _("Data t_ype"),
                              builtins, G_N_ELEMENTS(builtins), RAW_UNSIGNED_BYTE);
    gwy_param_def_add_int(paramdef, PARAM_BYTESWAP, "byteswap", _("Byte s_wap pattern"), 0, 64, 0);
    gwy_param_def_add_int(paramdef, PARAM_OFFSET, "offset", _("Start at _offset"), 0, 1 << 30, 0);
    gwy_param_def_add_int(paramdef, PARAM_SIZE, "size", _("_Sample size"), 1, 64, 8);
    gwy_param_def_add_int(paramdef, PARAM_SKIP, "skip", _("After each sample s_kip"), 0, 1 << 28, 0);
    gwy_param_def_add_int(paramdef, PARAM_ROWSKIP, "rowskip", _("After each _row skip"), 0, 1 << 28, 0);
    gwy_param_def_add_boolean(paramdef, PARAM_REVBYTE, "revbyte", _("_Reverse bits in bytes"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_REVSAMPLE, "revsample", _("Reverse bi_ts in samples"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_SIGN, "sign", _("Samples are si_gned"), FALSE);
}

static gint
rawfile_detect(void)
{
    gboolean takeover = FALSE;

    /* Read the setting direcly. Avoid instantiating a GwyParams object in file type detection. */
    gwy_container_gis_boolean_by_name(gwy_app_settings_get(), "/module/rawfile/takeover", &takeover);

    return takeover ? 1 : 0;
}

static GwyContainer*
rawfile_load(const gchar *filename,
             GwyRunType runtype,
             GError **error)
{
    ModuleArgs args;
    GwyContainer *data = NULL;
    GwyDialogOutcome outcome;
    GwyDataField *mask;
    GError *err = NULL;
    gsize size = 0;

    if (runtype != GWY_RUN_INTERACTIVE) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_INTERACTIVE,
                    _("Raw data import must be run as interactive."));
        return FALSE;
    }

    gwy_clear(&args, 1);
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(args.params, TRUE);

    if (!g_file_get_contents(filename, (gchar**)&args.file.buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    args.file.filename = filename;
    args.file.filesize = size;
    args.field = gwy_data_field_new(PREVIEW_SMALL_SIZE, PREVIEW_SMALL_SIZE, 1.0, 1.0, TRUE);

    outcome = run_gui(&args);

    gwy_params_save_to_settings(args.params);
    if (outcome != GWY_DIALOG_HAVE_RESULT) {
        err_CANCELLED(error);
        goto end;
    }

    data = gwy_container_new();
    gwy_container_set_object(data, gwy_app_get_data_key_for_id(0), args.field);
    if ((mask = gwy_app_channel_mask_of_nans(args.field, TRUE))) {
        gwy_container_pass_object(data, gwy_app_get_mask_key_for_id(0), mask);
    }
    gwy_file_channel_import_log_add(data, 0, NULL, filename);

end:
    GWY_OBJECT_UNREF(args.params);
    GWY_OBJECT_UNREF(args.field);
    g_free(args.file.buffer);

    return data;
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GtkWidget *vbox, *notebook, *hbox;
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyDialogOutcome outcome;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->field);

    gui.dialog = gwy_dialog_new(_("Read Raw File"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK, FALSE);

    hbox = gwy_hbox_new(8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 0);

    vbox = preview_box_new(&gui);
    gtk_box_pack_end(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(hbox), notebook, TRUE, TRUE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), dimensions_tab_new(&gui), gtk_label_new(_("Information")));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), format_tab_new(&gui), gtk_label_new(_("Data Format")));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), preset_tab_new(&gui), gtk_label_new(_("Presets")));
    gwy_select_in_filtered_inventory_treeeview(GTK_TREE_VIEW(gui.presetlist),
                                               gwy_params_get_string(args->params, PARAM_PRESET));

    g_signal_connect_swapped(gui.table_dims, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_format, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static GtkWidget*
preview_box_new(ModuleGUI *gui)
{
    GtkWidget *label, *vbox;

    vbox = gwy_vbox_new(2);

    label = gtk_label_new(_("Preview"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    gui->dataview = gwy_create_preview(gui->data, 0, PREVIEW_SMALL_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), gui->dataview, FALSE, FALSE, 0);

    gui->error = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(gui->error), 0.0, 0.5);
    gtk_label_set_line_wrap(GTK_LABEL(gui->error), TRUE);
    gtk_widget_set_size_request(gui->error, PREVIEW_SMALL_SIZE, -1);
    gtk_box_pack_start(GTK_BOX(vbox), gui->error, FALSE, FALSE, 0);

    return vbox;
}

static GtkWidget*
dimensions_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table;
    GwySIUnit *unit;
    GwySIValueFormat *vf;
    gchar *s;

    table = gui->table_dims = gwy_param_table_new(args->params);

    gwy_param_table_append_header(table, -1, _("File"));
    s = g_filename_display_basename(args->file.filename);
    gwy_param_table_append_info(table, INFO_FILE, s);
    g_free(s);
    gwy_param_table_info_set_add_punctuation(table, INFO_FILE, FALSE);
    unit = gwy_si_unit_new("B");
    vf = gwy_si_unit_get_format(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, args->file.filesize, NULL);
    s = g_strdup_printf("%.*f %s", vf->precision, args->file.filesize/vf->magnitude, vf->units);
    gwy_si_unit_value_format_free(vf);
    gwy_param_table_info_set_valuestr(table, INFO_FILE, s);
    g_object_unref(unit);
    g_free(s);

    gwy_param_table_append_header(table, -1, _("Resolution"));
    gwy_param_table_append_slider(table, PARAM_XRES);
    gwy_param_table_slider_set_mapping(table, PARAM_XRES, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_set_unitstr(table, PARAM_XRES, _("px"));
    gwy_param_table_append_slider(table, PARAM_YRES);
    gwy_param_table_slider_set_mapping(table, PARAM_YRES, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_set_unitstr(table, PARAM_YRES, _("px"));
    gwy_param_table_append_checkbox(table, PARAM_SQUARE_IMAGE);

    gwy_param_table_append_header(table, -1, _("Physical Dimensions"));
    gwy_param_table_append_slider(table, PARAM_XREAL);
    gwy_param_table_slider_set_mapping(table, PARAM_XREAL, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_YREAL);
    gwy_param_table_slider_set_mapping(table, PARAM_YREAL, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_checkbox(table, PARAM_SQUARE_PIXELS);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_ZSCALE);
    gwy_param_table_slider_set_mapping(table, PARAM_ZSCALE, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_entry(table, PARAM_MISSING_VALUE);
    gwy_param_table_add_enabler(table, PARAM_HAVE_MISSING, PARAM_MISSING_VALUE);

    gwy_param_table_append_header(table, -1, _("Units"));
    gwy_param_table_append_unit_chooser(table, PARAM_XYUNIT);
    gwy_param_table_append_unit_chooser(table, PARAM_ZUNIT);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_checkbox(table, PARAM_TAKE_OVER);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static GtkWidget*
format_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table;

    table = gui->table_format = gwy_param_table_new(args->params);

    /* Text */
    gwy_param_table_append_radio_item(table, PARAM_FORMAT, RAW_TEXT);
    gwy_param_table_append_slider(table, PARAM_LINEOFFSET);
    gwy_param_table_slider_set_mapping(table, PARAM_LINEOFFSET, GWY_SCALE_MAPPING_LOG1P);
    gwy_param_table_append_slider(table, PARAM_SKIPFIELDS);
    gwy_param_table_slider_set_mapping(table, PARAM_SKIPFIELDS, GWY_SCALE_MAPPING_LOG1P);
    gwy_param_table_set_unitstr(table, PARAM_SKIPFIELDS, _("fields"));
    gwy_param_table_append_combo(table, PARAM_DELIMITER_TYPE);
    gwy_param_table_append_entry(table, PARAM_DELIMITER);
    gwy_param_table_append_checkbox(table, PARAM_DECOMMA);

    /* Binary */
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_FORMAT, RAW_BINARY);
    gwy_param_table_append_combo(table, PARAM_BUILTIN);
    gwy_param_table_append_slider(table, PARAM_BYTESWAP);
    gwy_param_table_append_slider(table, PARAM_OFFSET);
    gwy_param_table_slider_set_mapping(table, PARAM_OFFSET, GWY_SCALE_MAPPING_LOG1P);
    gwy_param_table_set_unitstr(table, PARAM_OFFSET, _("bytes"));
    gwy_param_table_append_slider(table, PARAM_SIZE);
    gwy_param_table_set_unitstr(table, PARAM_SIZE, _("bits"));
    gwy_param_table_append_slider(table, PARAM_SKIP);
    gwy_param_table_slider_set_mapping(table, PARAM_SKIP, GWY_SCALE_MAPPING_LOG1P);
    gwy_param_table_set_unitstr(table, PARAM_SKIP, _("bits"));
    gwy_param_table_append_slider(table, PARAM_ROWSKIP);
    gwy_param_table_slider_set_mapping(table, PARAM_ROWSKIP, GWY_SCALE_MAPPING_LOG1P);
    gwy_param_table_set_unitstr(table, PARAM_ROWSKIP, _("bits"));
    gwy_param_table_append_checkbox(table, PARAM_REVBYTE);
    gwy_param_table_append_checkbox(table, PARAM_REVSAMPLE);
    gwy_param_table_append_checkbox(table, PARAM_SIGN);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static void
render_preset_cell(G_GNUC_UNUSED GtkTreeViewColumn *column,
                   GtkCellRenderer *cell,
                   GtkTreeModel *model,
                   GtkTreeIter *piter,
                   gpointer data)
{
    gulong id = GPOINTER_TO_UINT(data);
    GwyParamResource *preset;
    GwyParams *params;
    gchar *s;

    gtk_tree_model_get(model, piter, 0, &preset, -1);
    params = gwy_param_resource_get_params(preset);

    if (id == COLUMN_NAME)
        g_object_set(cell, "text", gwy_resource_get_name(GWY_RESOURCE(preset)), NULL);
    else if (id == COLUMN_TYPE) {
        RawFileFormat format = gwy_params_get_enum(params, PARAM_FORMAT);
        g_object_set(cell, "text", format == RAW_BINARY ? _("Binary") : _("Text"), NULL);
    }
    else if (id == COLUMN_SIZE) {
        gint xres = gwy_params_get_int(params, PARAM_XRES);
        gint yres = gwy_params_get_int(params, PARAM_YRES);

        s = g_strdup_printf("%u×%u", xres, yres);
        g_object_set(cell, "text", s, NULL);
        g_free(s);
    }
    else if (id == COLUMN_INFO) {
        RawFileFormat format = gwy_params_get_enum(params, PARAM_FORMAT);

        if (format == RAW_BINARY) {
            RawFileBuiltin builtin = gwy_params_get_enum(params, PARAM_BUILTIN);
            g_object_set(cell, "text", gwy_enum_to_string(builtin, builtins, G_N_ELEMENTS(builtins)), NULL);
        }
        else {
            const gchar *delim = gwy_params_get_string(params, PARAM_DELIMITER);

            if (!delim || !*delim)
                g_object_set(cell, "text", _("Delimiter: whitespace"), NULL);
            else {
                if (delim[1] == '\0' && !g_ascii_isgraph(delim[0]))
                    s = g_strdup_printf(_("Delimiter: 0x%02x"), delim[0]);
                else
                    s = g_strdup_printf(_("Delimiter: %s"), delim);
                g_object_set(cell, "text", s, NULL);
                g_free(s);
            }
        }
    }
    else {
        g_assert_not_reached();
    }
}

static GtkWidget*
preset_tab_new(ModuleGUI *gui)
{
    static const struct {
        const gchar *label;
        GCallback callback;
    }
    buttons[PRESET_NBUTTONS] = {
        { N_("verb|_Load"),  G_CALLBACK(load_preset),   },
        { N_("verb|_Store"), G_CALLBACK(save_preset),   },
        { N_("_Rename"),     G_CALLBACK(rename_preset), },
        { N_("_Delete"),     G_CALLBACK(delete_preset), },
    };

    static const GwyEnum columns[] = {
        { N_("Name"), COLUMN_NAME },
        { N_("Type"), COLUMN_TYPE },
        { N_("Size"), COLUMN_SIZE },
        { N_("Info"), COLUMN_INFO },
    };
    GwyInventoryStore *store;
    GtkTreeSelection *tselect;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeModel *filtermodel;
    GtkWidget *vbox, *label, *button, *scroll, *hbox;
    guint i;

    vbox = gwy_vbox_new(0);   /* to prevent notebook expanding tables */
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);

    store = gwy_inventory_store_new(gwy_resource_class_get_inventory(g_type_class_peek(preset_resource_type)));
    filtermodel = gwy_create_inventory_model_without_default(store);
    gui->presetlist = gtk_tree_view_new_with_model(filtermodel);
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(gui->presetlist), TRUE);
    g_object_unref(filtermodel);
    g_object_unref(store);

    for (i = 0; i < G_N_ELEMENTS(columns); i++) {
        renderer = gtk_cell_renderer_text_new();
        column = gtk_tree_view_column_new_with_attributes(_(columns[i].name), renderer, NULL);
        gtk_tree_view_column_set_cell_data_func(column, renderer,
                                                render_preset_cell, GUINT_TO_POINTER(columns[i].value), NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(gui->presetlist), column);
    }

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(scroll), gui->presetlist);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    hbox = gtk_hbutton_box_new();
    gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_START);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    for (i = 0; i < G_N_ELEMENTS(buttons); i++) {
        button = gui->preset_buttons[i] = gtk_button_new_with_mnemonic(gwy_sgettext(buttons[i].label));
        gtk_container_add(GTK_CONTAINER(hbox), button);
        g_signal_connect_swapped(button, "clicked", buttons[i].callback, gui);
    }

    hbox = gwy_hbox_new(6);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 4);

    label = gtk_label_new_with_mnemonic(_("Preset _name:"));
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gui->presetname = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(gui->presetname), gwy_params_get_string(gui->args->params, PARAM_PRESET));
    gtk_entry_set_max_length(GTK_ENTRY(gui->presetname), 40);
    gtk_box_pack_start(GTK_BOX(hbox), gui->presetname, FALSE, FALSE, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), gui->presetname);

    gui->preset_error = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(gui->preset_error), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), gui->preset_error, FALSE, FALSE, 4);
    set_widget_as_warning_message(gui->preset_error);

    tselect = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->presetlist));
    gtk_tree_selection_set_mode(tselect, GTK_SELECTION_SINGLE);
    g_signal_connect_swapped(tselect, "changed", G_CALLBACK(preset_selected), gui);

    return vbox;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    static const guint text_params[] = {
        PARAM_LINEOFFSET, PARAM_SKIPFIELDS, PARAM_DELIMITER_TYPE, PARAM_DELIMITER, PARAM_DECOMMA,
    };
    static const guint binary_params[] = {
        PARAM_BUILTIN, PARAM_BYTESWAP, PARAM_OFFSET, PARAM_SIZE, PARAM_SKIP, PARAM_ROWSKIP, PARAM_REVBYTE,
        PARAM_REVSAMPLE, PARAM_SIGN,
    };

    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    RawFileFormat format = gwy_params_get_enum(params, PARAM_FORMAT);
    RawFileBuiltin builtin = gwy_params_get_enum(params, PARAM_BUILTIN);
    gboolean square_image = gwy_params_get_boolean(params, PARAM_SQUARE_IMAGE);
    gboolean square_pixels = gwy_params_get_boolean(params, PARAM_SQUARE_PIXELS);
    gint xres = gwy_params_get_int(params, PARAM_XRES);
    gint yres = gwy_params_get_int(params, PARAM_YRES);
    gint power10;
    guint i;

    if (id < 0 || id == PARAM_FORMAT || id == PARAM_BUILTIN) {
        /* This makes too many thing sensitive. We made them insensitive below. */
        for (i = 0; i < G_N_ELEMENTS(text_params); i++)
            gwy_param_table_set_sensitive(gui->table_format, text_params[i], format == RAW_TEXT);
        for (i = 0; i < G_N_ELEMENTS(binary_params); i++)
            gwy_param_table_set_sensitive(gui->table_format, binary_params[i], format == RAW_BINARY);

        if (builtin) {
            gwy_param_table_set_sensitive(gui->table_format, PARAM_SIZE, FALSE);
            gwy_param_table_set_sensitive(gui->table_format, PARAM_REVSAMPLE, FALSE);
            gwy_param_table_set_sensitive(gui->table_format, PARAM_SIGN, FALSE);
        }
        if (builtin_size[builtin] <= 8)
            gwy_param_table_set_sensitive(gui->table_format, PARAM_BYTESWAP, FALSE);
    }
    if (format == RAW_TEXT) {
        gwy_param_table_set_sensitive(gui->table_format, PARAM_DELIMITER,
                                      gwy_params_get_enum(params, PARAM_DELIMITER_TYPE) == RAW_DELIM_OTHER);
    }

    /* FIXME: Previously, switching from user to builtin and back restored the user format size and sign. */
    if (id < 0 || id == PARAM_BUILTIN) {
        gwy_param_table_slider_restrict_range(gui->table_format, PARAM_SIZE, 1, builtin ? 64 : 56);
    }
    if ((id < 0 || id == PARAM_BUILTIN) && builtin) {
        gboolean is_signed = (builtin == RAW_SIGNED_BYTE || builtin == RAW_SIGNED_WORD16
                              || builtin == RAW_SIGNED_WORD32 || builtin == RAW_SIGNED_WORD64);
        guint maxswap = builtin_size[builtin]/8 - 1;

        gwy_param_table_set_int(gui->table_format, PARAM_SIZE, builtin_size[builtin]);
        gwy_param_table_set_boolean(gui->table_format, PARAM_SIGN, is_signed);
        gwy_param_table_slider_restrict_range(gui->table_format, PARAM_BYTESWAP, 0, maxswap ? maxswap : 1);
        if (!maxswap)
            gwy_param_table_set_int(gui->table_format, PARAM_BYTESWAP, 0);
    }
    if (id < 0 || id == PARAM_BUILTIN) {
        if (builtin) {
            gwy_param_table_set_int(gui->table_format, PARAM_SKIP, gwy_params_get_int(params, PARAM_SKIP)/8*8);
            gwy_param_table_set_int(gui->table_format, PARAM_ROWSKIP, gwy_params_get_int(params, PARAM_ROWSKIP)/8*8);
        }
        gwy_param_table_slider_set_steps(gui->table_format, PARAM_SKIP, builtin ? 8 : 1, builtin ? 64 : 8);
        gwy_param_table_slider_set_steps(gui->table_format, PARAM_ROWSKIP, builtin ? 8 : 1, builtin ? 64 : 8);
    }

    if (id < 0 || id == PARAM_DELIMITER_TYPE) {
        RawFileDelimiter delim = gwy_params_get_enum(params, PARAM_DELIMITER_TYPE);
        if (delim == RAW_DELIM_TAB)
            gwy_param_table_set_string(gui->table_format, PARAM_DELIMITER, "\t");
        else if (delim == RAW_DELIM_WHITESPACE)
            gwy_param_table_set_string(gui->table_format, PARAM_DELIMITER, "");
    }

    if (square_image) {
        if (id == PARAM_YRES)
            gwy_param_table_set_int(gui->table_dims, PARAM_XRES, (xres = yres));
        else if (id == PARAM_XRES || id == PARAM_SQUARE_IMAGE)
            gwy_param_table_set_int(gui->table_dims, PARAM_YRES, (yres = xres));
    }

    if (square_pixels) {
        gdouble xreal = gwy_params_get_double(params, PARAM_XREAL);
        gdouble yreal = gwy_params_get_double(params, PARAM_YREAL);

        if (id == PARAM_YRES || id == PARAM_SQUARE_IMAGE || id == PARAM_XREAL || id == PARAM_SQUARE_PIXELS)
            gwy_param_table_set_double(gui->table_dims, PARAM_YREAL, (yreal = xreal/xres*yres));
        else if (id == PARAM_XRES || id == PARAM_YREAL)
            gwy_param_table_set_double(gui->table_dims, PARAM_XREAL, (xreal = yreal/yres*xres));
    }

    if (id < 0 || id == PARAM_XYUNIT) {
        GwySIUnit *unit = gwy_params_get_unit(params, PARAM_XYUNIT, &power10);
        GwySIValueFormat *vf = gwy_si_unit_get_format_for_power10(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, power10, NULL);

        gwy_param_table_set_unitstr(gui->table_dims, PARAM_XREAL, vf->units);
        gwy_param_table_set_unitstr(gui->table_dims, PARAM_YREAL, vf->units);
        gwy_si_unit_value_format_free(vf);
    }
    if (id < 0 || id == PARAM_ZUNIT) {
        GwySIUnit *unit = gwy_params_get_unit(params, PARAM_ZUNIT, &power10);
        GwySIValueFormat *vf = gwy_si_unit_get_format_for_power10(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, power10, NULL);

        gwy_param_table_set_unitstr(gui->table_dims, PARAM_ZSCALE, vf->units);
        gwy_si_unit_value_format_free(vf);
    }

    if (id < 0 || id == PARAM_FORMAT || id == PARAM_XRES || id == PARAM_YRES
        || id == PARAM_SKIPFIELDS || id == PARAM_LINEOFFSET || id == PARAM_DELIMITER || id == PARAM_DELIMITER_TYPE
        || id == PARAM_BUILTIN || id == PARAM_SIZE || id == PARAM_SKIP || id == PARAM_ROWSKIP || id == PARAM_OFFSET)
        gtk_label_set_text(GTK_LABEL(gui->error), "");
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyDataField *field, *mask;
    GError *error = NULL;

    if ((field = read_data_field(args, &error))) {
        /* Fix any NaNs in the data for preview. */
        if ((mask = gwy_app_channel_mask_of_nans(field, TRUE)))
            g_object_unref(mask);
        gwy_data_field_assign(args->field, field);
        g_object_unref(field);
        gtk_label_set_text(GTK_LABEL(gui->error), "");
        gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, TRUE);
    }
    else {
        gwy_data_field_resample(args->field, PREVIEW_SMALL_SIZE, PREVIEW_SMALL_SIZE, GWY_INTERPOLATION_NONE);
        gwy_data_field_clear(args->field);
        gtk_label_set_markup(GTK_LABEL(gui->error), error->message);
        g_clear_error(&error);
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, FALSE);
    }
    gwy_data_field_data_changed(args->field);
    gwy_set_data_preview_size(GWY_DATA_VIEW(gui->dataview), PREVIEW_SMALL_SIZE);
}

/* NB: Does not called when the row changes (e.g. preset is renamed), but the selection stays the same. */
static void
preset_selected(ModuleGUI *gui)
{
    GwyParamResource *preset;
    GtkTreeModel *store;
    GtkTreeSelection *tselect;
    GtkTreeIter iter;
    const gchar *name = "";
    gboolean sens = FALSE;

    tselect = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->presetlist));
    g_return_if_fail(tselect);
    if (gtk_tree_selection_get_selected(tselect, &store, &iter)) {
        gtk_tree_model_get(store, &iter, 0, &preset, -1);
        name = gwy_resource_get_name(GWY_RESOURCE(preset));
        sens = TRUE;
    }
    gwy_params_set_resource(gui->args->params, PARAM_PRESET, name);
    gtk_entry_set_text(GTK_ENTRY(gui->presetname), name);

    gtk_widget_set_sensitive(gui->preset_buttons[PRESET_BUTTON_LOAD], sens);
    gtk_widget_set_sensitive(gui->preset_buttons[PRESET_BUTTON_DELETE], sens);
    gtk_widget_set_sensitive(gui->preset_buttons[PRESET_BUTTON_RENAME], sens);
}

static void
load_preset(ModuleGUI *gui)
{
    GwyParams *params;
    GwyParamResource *preset;
    GtkTreeModel *store;
    GtkTreeSelection *tselect;
    GtkTreeIter iter;
    gint xres, yres;
    gdouble xreal, yreal;
    gchar *delim;

    tselect = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->presetlist));
    if (!gtk_tree_selection_get_selected(tselect, &store, &iter))
        return;

    gtk_tree_model_get(store, &iter, 0, &preset, -1);
    params = gwy_param_resource_get_params(preset);
    xres = gwy_params_get_int(params, PARAM_XRES);
    yres = gwy_params_get_int(params, PARAM_YRES);
    xreal = gwy_params_get_double(params, PARAM_XREAL);
    yreal = gwy_params_get_double(params, PARAM_YREAL);
    /* Delimiter is complicated because param_changed() will reset the string according to DELIMTER_TYPE. */
    delim = g_strdup(gwy_params_get_string(params, PARAM_DELIMITER));
    sanitise_params(params, FALSE);
    gwy_param_table_set_from_params(gui->table_dims, params);
    gwy_param_table_set_from_params(gui->table_format, params);

    if (xres != yres)
        gwy_param_table_set_boolean(gui->table_dims, PARAM_SQUARE_IMAGE, FALSE);
    if (fabs(log(fabs(xres/xreal*yreal/yres))) > EPS)
        gwy_param_table_set_boolean(gui->table_dims, PARAM_SQUARE_PIXELS, FALSE);
    gwy_param_table_set_enum(gui->table_format, PARAM_DELIMITER_TYPE, delimiter_string_to_type(delim));
    g_free(delim);
}

static void
save_preset(ModuleGUI *gui)
{
    GwyParamResource *preset;
    GwyInventory *inventory;
    const gchar *name;

    name = gtk_entry_get_text(GTK_ENTRY(gui->presetname));
    if (!preset_validate_name(gui, name, TRUE))
        return;

    /* XXX: This part is completely generic, should work for any parameter preset and can be put to some library
     * function. */
    gwy_debug("Now I'm saving `%s'", name);
    inventory = gwy_resource_class_get_inventory(g_type_class_peek(preset_resource_type));
    preset = gwy_inventory_get_item(inventory, name);
    if (!preset) {
        gwy_debug("Appending `%s'", name);
        preset = g_object_new(preset_resource_type, "name", name, NULL);
        gwy_params_assign(gwy_param_resource_get_params(preset), gui->args->params);
        gwy_inventory_insert_item(inventory, preset);
        g_object_unref(preset);
    }
    else {
        gwy_debug("Setting `%s'", name);
        gwy_params_assign(gwy_param_resource_get_params(preset), gui->args->params);
    }
    /* FIXME: Handle error? Show it in gui->preset_error? */
    gwy_resource_save(GWY_RESOURCE(preset), NULL);

    gwy_select_in_filtered_inventory_treeeview(GTK_TREE_VIEW(gui->presetlist), name);
    preset_selected(gui);
}

static void
rename_preset(ModuleGUI *gui)
{
    GwyParamResource *preset;
    GwyInventory *inventory;
    GtkTreeModel *model;
    GtkTreeSelection *tselect;
    GtkTreeIter iter;
    const gchar *newname, *oldname;
    gboolean ok = FALSE;

    tselect = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->presetlist));
    if (!gtk_tree_selection_get_selected(tselect, &model, &iter))
        return;

    newname = gtk_entry_get_text(GTK_ENTRY(gui->presetname));
    if (!preset_validate_name(gui, newname, TRUE))
        return;

    gtk_tree_model_get(model, &iter, 0, &preset, -1);

    /* XXX: This part is completely generic, should work for any parameter preset and can be put to some library
     * function. */
    inventory = gwy_resource_class_get_inventory(g_type_class_peek(preset_resource_type));
    oldname = gwy_resource_get_name(GWY_RESOURCE(preset));
    if (!gwy_strequal(newname, oldname) && !gwy_inventory_get_item(inventory, newname)) {
        gwy_debug("Now I will rename `%s' to `%s'", oldname, newname);
        ok = gwy_resource_rename(GWY_RESOURCE(preset), newname);
    }

    if (ok) {
        gwy_select_in_filtered_inventory_treeeview(GTK_TREE_VIEW(gui->presetlist), newname);
        preset_selected(gui);
    }
}

static void
delete_preset(ModuleGUI *gui)
{
    GwyParamResource *preset;
    GtkTreeModel *model;
    GtkTreeSelection *tselect;
    GtkTreeIter iter;

    tselect = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->presetlist));
    if (!gtk_tree_selection_get_selected(tselect, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, 0, &preset, -1);
    gwy_resource_delete(GWY_RESOURCE(preset));
}

static gboolean
preset_validate_name(ModuleGUI *gui,
                     const gchar *name,
                     gboolean show_warning)
{
    gboolean ok;

    ok = (name && *name && !strchr(name, '/') && !strchr(name, '\\'));
    if (show_warning)
        gtk_label_set_text(GTK_LABEL(gui->preset_error), ok ? "" : _("Invalid preset name."));

    return ok;
}

static gboolean
missingval_is_number(const gchar *missingval)
{
    gchar *end;

    g_strtod(missingval, &end);
    if (end == missingval)
        return FALSE;

    return TRUE;
}

static GwyDataField*
read_data_field(ModuleArgs *args, GError **error)
{
    RawFileFile *file = &args->file;
    GwyParams *params = args->params;
    RawFileFormat format = gwy_params_get_enum(params, PARAM_FORMAT);
    RawFileBuiltin builtin = gwy_params_get_enum(params, PARAM_BUILTIN);
    gulong size = (builtin ? builtin_size[builtin] : gwy_params_get_int(params, PARAM_SIZE));
    const gchar *delim = gwy_params_get_string(params, PARAM_DELIMITER);
    const gchar *missingval = gwy_params_get_string(params, PARAM_MISSING_VALUE);
    const gboolean havemissing = gwy_params_get_boolean(params, PARAM_HAVE_MISSING);
    guint xres = gwy_params_get_int(params, PARAM_XRES);
    guint yres = gwy_params_get_int(params, PARAM_YRES);
    gulong skip = gwy_params_get_int(params, PARAM_SKIP);
    gulong rowskip = gwy_params_get_int(params, PARAM_ROWSKIP);
    gulong skipfields = gwy_params_get_int(params, PARAM_SKIPFIELDS);
    gulong offset = gwy_params_get_int(params, PARAM_OFFSET);
    gulong lineoffset = gwy_params_get_int(params, PARAM_LINEOFFSET);
    gdouble xreal = gwy_params_get_double(params, PARAM_XREAL);
    gdouble yreal = gwy_params_get_double(params, PARAM_YREAL);
    GwyDataField *field;
    GError *err = NULL;
    GwySIUnit *siunit;
    gulong reqsize;
    gint power10;
    guint i, n;
    gdouble *d;

    if (format == RAW_BINARY) {
        gulong bitlen, bitrowstride = (size + skip)*xres + rowskip;

        if (builtin && bitrowstride % 8) {
            g_warning("rowstride is not a whole number of bytes");
            bitrowstride = ((bitrowstride + 7)/8)*8;
        }
        /* We do not have to skip after the last sample (and the last row). So subtract them. */
        bitlen = yres*bitrowstride;
        if (yres && xres)
            bitlen -= skip + rowskip;
        reqsize = offset + bitlen/8;
    }
    else {
        gulong rowstride = (xres + skipfields)*(1 + MAX(strlen(delim), 1));

        reqsize = lineoffset + yres*rowstride;
    }

    if (reqsize > file->filesize) {
        g_set_error(error, error_domain, RAWFILE_ERROR_TOO_SHORT,
                    _("<b>Too short file</b>\n"
                      "The format would require %lu bytes long file (at least), "
                      "but the length of `%s' is only %u bytes."),
                    reqsize, file->filename, file->filesize);
        return NULL;
    }

    field = gwy_data_field_new(xres, yres, 1.0, 1.0, TRUE);

    siunit = gwy_params_get_unit(params, PARAM_XYUNIT, &power10);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(field), siunit);
    gwy_data_field_set_xreal(field, pow10(power10)*xreal);
    gwy_data_field_set_yreal(field, pow10(power10)*yreal);
    gwy_debug("dims %g x %g [10^%d because of %s]",
              xreal, yreal, power10, gwy_params_get_string(params, PARAM_XYUNIT));

    siunit = gwy_params_get_unit(params, PARAM_ZUNIT, &power10);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(field), siunit);
    gwy_debug("zscale %g [10^%d because of %s]",
              gwy_params_get_double(params, PARAM_ZSCALE), power10, gwy_params_get_string(params, PARAM_ZUNIT));

    d = gwy_data_field_get_data(field);
    if (format == RAW_BINARY) {
        if (builtin)
            rawfile_read_builtin(args, d);
        else
            rawfile_read_bits(args, d);
    }
    else {
        if (!rawfile_read_ascii(args, d, &err)) {
            g_set_error(error, error_domain, RAWFILE_ERROR_TEXT_PARSING,
                        _("<b>Parsing failed</b>\n"
                          "The contents of `%s' does not match format: %s."),
                        file->filename, err->message);
            g_clear_error(&err);
            GWY_OBJECT_UNREF(field);
        }
    }

    if (field && havemissing && missingval_is_number(missingval)) {
        gdouble mv = g_strtod(missingval, NULL);

        /* This works for integral missingvalue, which is the most useful case. Floating point data can use actual
         * NaNs and do not need replacement of special values.  Use double-negation in case someone explicitly
         * specifies NaN as the missing value. */
        n = xres * yres;
        for (i = 0; i < n; i++) {
            if (!(d[i] != mv))
                d[i] = NAN;
        }
    }
    if (field)
        gwy_data_field_multiply(field, pow10(power10)*gwy_params_get_double(params, PARAM_ZSCALE));

    return field;
}

static inline guint32
reverse_bits(guint32 x, guint n)
{
    gulong y = 0;

    while (n--) {
        y <<= 1;
        y |= x&1;
        x >>= 1;
    }
    return y;
}

/* FIXME: The maximum size this can handle is 56 bits because we are sloppy. */
static void
rawfile_read_bits(ModuleArgs *args, gdouble *data)
{
    GwyParams *params = args->params;
    guint size = gwy_params_get_int(params, PARAM_SIZE);
    guint skip = gwy_params_get_int(params, PARAM_SKIP);
    guint rowskip = gwy_params_get_int(params, PARAM_ROWSKIP);
    guint xres = gwy_params_get_int(params, PARAM_XRES);
    guint yres = gwy_params_get_int(params, PARAM_YRES);
    gboolean revbyte = gwy_params_get_boolean(params, PARAM_REVBYTE);
    gboolean revsample = gwy_params_get_boolean(params, PARAM_REVSAMPLE);
    gboolean sign = gwy_params_get_boolean(params, PARAM_SIGN);
    const guchar *buffer = args->file.buffer + gwy_params_get_int(params, PARAM_OFFSET);
    guchar *rtable = NULL;
    guchar *rtable8 = NULL;
    guint64 *bitmask = NULL;
    guint i, j, nb;
    guint64 b, bucket, x, rem;

    g_assert(size <= 56);
    g_assert(size > 1 || !sign);

    if (revsample && size <= 8) {
        rtable = g_new(guchar, 1 << size);
        for (i = 0; i < 1 << size; i++)
            rtable[i] = reverse_bits(i, size);
    }
    if (revbyte) {
        rtable8 = g_new(guchar, 1 << 8);
        for (i = 0; i < 1 << 8; i++)
            rtable8[i] = reverse_bits(i, 8);
    }
    /* bitmask[n] has n bits set. */
    bitmask = g_new(guint64, 65);
    bucket = 0;
    for (i = 0; i <= 64; i++) {
        bitmask[i] = bucket;
        bucket <<= 1U;
        bucket |= G_GUINT64_CONSTANT(1);
    }

    nb = 0;
    bucket = 0;

    for (i = yres; i; i--) {
        for (j = xres; j; j--) {
            /* gather enough bits, new bits are put to the least significant position */
            while (nb < size) {
                b = *(buffer++);
                if (revbyte)
                    b = rtable8[b];
                bucket <<= 8;
                bucket |= b;
                nb += 8;
            }
            /* we have this many too much bits now (in the least significat part of bucket) */
            rem = nb - size;
            /* x is the data sample (in the most significat part of bucket) */
            x = bucket >> rem;
            if (revsample) {
                if (rtable)
                    x = rtable[x];
                else
                    x = reverse_bits(x, size);
            }
            /* rem bits remains in bucket */
            bucket &= bitmask[rem];
            nb = rem;

            /* sign-extend to 64bit signed number if signed (i.e. if the highest bit of sample is set, set bits of
             * x all the way up to the 63th) */
            if (sign) {
                if (x & ~bitmask[size-1]) {
                    union { guint64 u; gint64 s; } xx;

                    xx.u = x | ~bitmask[size];
                    *(data++) = (gdouble)xx.s;
                }
                else
                    *(data++) = (gdouble)x;
            }
            else
                *(data++) = (gdouble)x;

            if (!j && !i)
                break;

            /* skip skip bits, only the last byte is important */
            if (nb < skip) {
                /* skip what we have in the bucket */
                rem = skip - nb;
                /* whole bytes */
                buffer += rem/8;
                rem %= 8;  /* remains to skip */
                nb = 8 - rem;  /* so this number of bits will be in bucket */
                b = *(buffer++);
                if (revbyte)
                    b = rtable8[b];
                bucket = b & bitmask[nb];
            }
            else {
                /* we have enough bits in bucket, so just get rid of the extra ones */
                nb -= skip;
                bucket &= bitmask[nb];
            }
        }
        if (!i)
            break;

        /* skip rowskip bits, only the last byte is important */
        if (nb < rowskip) {
            /* skip what we have in the bucket */
            rem = rowskip - nb;
            /* whole bytes */
            buffer += rem/8;
            rem %= 8;  /* remains to skip */
            nb = 8 - rem;  /* so this number of bits will be in bucket */
            b = *(buffer++);
            if (revbyte)
                b = rtable8[b];
            bucket = b & bitmask[nb];
        }
        else {
            /* we have enough bits in bucket, so just get rid of the extra ones */
            nb -= rowskip;
            bucket &= bitmask[nb];
        }
    }
    g_free(rtable8);
    g_free(rtable);
    g_free(bitmask);
}

#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
static inline gdouble
get_pascal_real_native(const guchar *p)
{
    gdouble x;

    if (!p[0])
        return 0.0;

    x = 1.0 + ((((p[1]/256.0 + p[2])/256.0 + p[3])/256.0 + p[4])/256.0 + (p[5] & 0x7f))/128.0;
    if (p[5] & 0x80)
        x = -x;

    return x*gwy_powi(2.0, (gint)p[0] - 129);
}

static inline gdouble
get_half_native(const guchar *p)
{
    gdouble x = p[0]/1024.0 + (p[1] & 0x03)/4.0;
    gint exponent = (p[1] >> 2) & 0x1f;

    if (G_UNLIKELY(exponent == 0x1f)) {
        if (x)
            return NAN;
        return (p[1] & 0x80) ? -HUGE_VAL : HUGE_VAL;
    }

    if (exponent)
        x = (1.0 + x)*gwy_powi(2.0, exponent - 15);
    else
        x = x/16384.0;

    return (p[1] & 0x80) ? -x : x;
}
#endif

#if (G_BYTE_ORDER == G_BIG_ENDIAN)
static inline gdouble
get_pascal_real_native(const guchar *p)
{
    gdouble x;

    if (!p[5])
        return 0.0;

    x = 1.0 + ((((p[4]/256.0 + p[3])/256.0 + p[2])/256.0 + p[1])/256.0 + (p[0] & 0x7f))/128.0;
    if (p[0] & 0x80)
        x = -x;

    return x*gwy_powi(2.0, (gint)p[5] - 129);
}

static inline gdouble
get_half_native(const guchar *p)
{
    gdouble x = p[1]/1024.0 + (p[0] & 0x03)/4.0;
    gint exponent = (p[0] >> 2) & 0x1f;

    if (G_UNLIKELY(exponent == 0x1f)) {
        if (x)
            return NAN;
        return (p[0] & 0x80) ? -HUGE_VAL : HUGE_VAL;
    }

    if (exponent)
        x = (1.0 + x)*gwy_powi(2.0, exponent - 15);
    else
        x = x/16384.0;

    return (p[0] & 0x80) ? -x : x;
}
#endif

static void
rawfile_read_builtin(ModuleArgs *args, gdouble *data)
{
    GwyParams *params = args->params;
    RawFileBuiltin builtin = gwy_params_get_enum(params, PARAM_BUILTIN);
    guint xres = gwy_params_get_int(params, PARAM_XRES);
    guint yres = gwy_params_get_int(params, PARAM_YRES);
    guint skip = gwy_params_get_int(params, PARAM_SKIP);
    guint rowskip = gwy_params_get_int(params, PARAM_ROWSKIP);
    guint byteswap = gwy_params_get_int(params, PARAM_BYTESWAP);
    gboolean revbyte = gwy_params_get_boolean(params, PARAM_REVBYTE);
    const guchar *buffer = args->file.buffer + gwy_params_get_int(params, PARAM_OFFSET);
    guchar *rtable8 = NULL;
    union {
        gint8 i8; guint8 u8; gint16 i16; guint16 u16; gint32 i32; guint32 u32; gint64 i64; guint64 u64;
        gfloat f; gdouble d;
    } good_alignment;
    guint i, j, k, size;
    guchar *b;

    g_return_if_fail(builtin && builtin < RAW_NTYPES);
    size = builtin_size[builtin];
    g_return_if_fail(size <= 64 && size % 8 == 0);
    g_return_if_fail(skip % 8 == 0);
    g_return_if_fail(rowskip % 8 == 0);

    if (revbyte) {
        rtable8 = g_new(guchar, 1 << 8);
        for (i = 0; i < 1 << 8; i++)
            rtable8[i] = reverse_bits(i, 8);
    }

    size = size/8;
    skip = skip/8;
    rowskip = rowskip/8;

    gwy_clear(&good_alignment, 1);
    b = (guchar*)&good_alignment;

    for (i = yres; i; i--) {
        for (j = xres; j; j--) {
            /* The XOR magic puts each byte where it belongs according to byteswap. */
            if (revbyte) {
                for (k = 0; k < size; k++)
                    b[k ^ byteswap] = rtable8[*(buffer++)];
            }
            else {
                for (k = 0; k < size; k++)
                    b[k ^ byteswap] = *(buffer++);
            }
            /* Now interpret b as a number in native byte order. */
            if (builtin == RAW_SIGNED_BYTE)
                *(data++) = (gdouble)good_alignment.i8;
            else if (builtin == RAW_UNSIGNED_BYTE)
                *(data++) = (gdouble)good_alignment.u8;
            else if (builtin == RAW_SIGNED_WORD16)
                *(data++) = (gdouble)good_alignment.i16;
            else if (builtin == RAW_UNSIGNED_WORD16)
                *(data++) = (gdouble)good_alignment.u16;
            else if (builtin == RAW_SIGNED_WORD32)
                *(data++) = (gdouble)good_alignment.i32;
            else if (builtin == RAW_UNSIGNED_WORD32)
                *(data++) = (gdouble)good_alignment.u32;
            else if (builtin == RAW_IEEE_FLOAT)
                *(data++) = (gdouble)good_alignment.f;
            else if (builtin == RAW_IEEE_DOUBLE)
                *(data++) = (gdouble)good_alignment.d;
            else if (builtin == RAW_SIGNED_WORD64)
                *(data++) = (gdouble)good_alignment.i64;
            else if (builtin == RAW_UNSIGNED_WORD64)
                *(data++) = (gdouble)good_alignment.u64;
            else if (builtin == RAW_IEEE_HALF)
                *(data++) = (gdouble)get_half_native(b);
            else if (builtin == RAW_PASCAL_REAL)
                *(data++) = (gdouble)get_pascal_real_native(b);
            buffer += skip;
        }
        buffer += rowskip;
    }
    g_free(rtable8);
}

static gboolean
rawfile_read_ascii(ModuleArgs *args, gdouble *data, GError **error)
{
    GwyParams *params = args->params;
    guint xres = gwy_params_get_int(params, PARAM_XRES);
    guint yres = gwy_params_get_int(params, PARAM_YRES);
    guint lineoffset = gwy_params_get_int(params, PARAM_LINEOFFSET);
    guint skipfields = gwy_params_get_int(params, PARAM_SKIPFIELDS);
    const gchar *delim = gwy_params_get_string(params, PARAM_DELIMITER);
    const gchar *missingval = gwy_params_get_string(params, PARAM_MISSING_VALUE);
    gboolean havemissing = gwy_params_get_boolean(params, PARAM_HAVE_MISSING);
    gboolean decomma = gwy_params_get_boolean(params, PARAM_DECOMMA);
    RawStrtodFunc strtod_func = (decomma ? gwy_comma_strtod : gwy_ascii_strtod);
    const guchar *buffer = args->file.buffer;
    guint i, j, n;
    gint cdelim = '\0';
    gint delimlen;
    gdouble x;
    guchar *end;

    if (!havemissing || missingval_is_number(missingval))
        missingval = NULL;

    /* Update locale data before parsing. */
    decimal_point = gwy_get_decimal_separator();
    decimal_point_len = strlen(decimal_point);

    g_assert(decimal_point_len != 0);

    /* Skip lines at the beginning. */
    for (i = 0; i < lineoffset; i++) {
        buffer = strchr(buffer, '\n');
        if (!buffer) {
            g_set_error(error, error_domain, RAWFILE_ERROR_TEXT_PARSING,
                        _("Not enough lines (%d) for offset (%d)"),
                        i, lineoffset);
            return FALSE;
        }
        buffer++;
    }

    /* No delimiter means any whitespace. */
    if (!delim)
        delimlen = 0;
    else {
        delimlen = strlen(delim);
        cdelim = delim[0];
    }

    for (n = 0; n < yres; n++) {
        /* Skip fields. */
        if (!delimlen) {
            buffer += strspn(buffer, " \t\n\r");
            for (i = 0; i < skipfields; i++) {
                j = strcspn(buffer, " \t\n\r");
                buffer += j;
                j = strspn(buffer, " \t\n\r");
                if (!j) {
                    g_set_error(error, error_domain, RAWFILE_ERROR_TEXT_PARSING,
                                _("Expected whitespace to skip more fields in row %u, got `%.16s'"),
                                n, buffer);
                    return FALSE;
                }
                buffer += j;
            }
        }
        else if (delimlen == 1) {
            for (i = 0; i < skipfields; i++) {
                end = strchr(buffer, cdelim);
                if (!end) {
                    g_set_error(error, error_domain, RAWFILE_ERROR_TEXT_PARSING,
                                _("Expected `%c' to skip more fields in row %u, got `%.16s'"),
                                cdelim, n, buffer);
                    return FALSE;
                }
                buffer = end + 1;
            }
        }
        else {
            for (i = 0; i < skipfields; i++) {
                end = strstr(buffer, delim);
                if (!end) {
                    g_set_error(error, error_domain, RAWFILE_ERROR_TEXT_PARSING,
                                _("Expected `%s' to skip more fields in row %u, got `%.16s'"),
                                delim, n, buffer);
                    return FALSE;
                }
                buffer = end + delimlen;
            }
        }

        /* Read data. */
        if (!delimlen) {
            for (i = 0; i < xres; i++) {
                x = strtod_func(buffer, missingval, (char**)&end);
                if (end == buffer) {
                    g_set_error(error, error_domain, RAWFILE_ERROR_TEXT_PARSING,
                                _("Garbage `%.16s' in row %u, column %u"),
                                buffer, n, i);
                    return FALSE;
                }
                buffer = end;
                *(data++) = x;
            }
        }
        else if (delimlen == 1) {
            for (i = 0; i < xres; i++) {
                x = strtod_func(buffer, missingval, (char**)&end);
                if (end == buffer) {
                    g_set_error(error, error_domain, RAWFILE_ERROR_TEXT_PARSING,
                                _("Garbage `%.16s' in row %u, column %u"),
                                buffer, n, i);
                    return FALSE;
                }
                buffer = end + strspn(end, " \t");
                if (*buffer == cdelim)
                    buffer++;
                else if (i + 1 == xres
                         && (j = strspn(buffer, "\n\r")))
                    buffer += j;
                else {
                    g_set_error(error, error_domain, RAWFILE_ERROR_TEXT_PARSING,
                                _("Expected delimiter `%c' after data in row %u, column %u, got `%c'"),
                                cdelim, n, i, *buffer);
                    return FALSE;
                }
                *(data++) = x;
            }
        }
        else {
            for (i = 0; i < xres; i++) {
                x = strtod_func(buffer, missingval, (char**)&end);
                if (end == buffer) {
                    g_set_error(error, error_domain, RAWFILE_ERROR_TEXT_PARSING,
                                _("Garbage `%.16s' in row %u, column %u"),
                                buffer, n, i);
                    return FALSE;
                }
                buffer = end + strspn(end, " \t");
                if (strncmp(buffer, delim, delimlen) == 0)
                    buffer += delimlen;
                else if (i + 1 == xres
                         && (j = strspn(buffer, "\n\r")))
                    buffer += j;
                else {
                    g_set_error(error, error_domain, RAWFILE_ERROR_TEXT_PARSING,
                                _("Expected delimiter `%s' after data in row %u, column %u, got `%.16s'"),
                                delim, n, i, buffer);
                    return FALSE;
                }
                *(data++) = x;
            }
        }
    }

    return TRUE;
}

static gdouble
gwy_ascii_strtod(const gchar *nptr, const gchar *missingval, gchar **endptr)
{
    if (missingval) {
        gint len = strlen(missingval);
        const gchar *p = nptr;

        while (g_ascii_isspace(*p))
            p++;

        if (strncmp(p, missingval, len) == 0) {
            if (endptr)
                *endptr = (gchar*)p + len;
            return NAN;
        }
    }

    return g_ascii_strtod(nptr, endptr);
}

static gdouble
gwy_comma_strtod(const gchar *nptr, const gchar *missingval, gchar **endptr)
{
    enum { FIXED_BUFFER_LEN = G_ASCII_DTOSTR_BUF_SIZE };
    const gchar *p, *decimal_point_pos, *end = NULL;     /* Silence gcc */
    gchar fixed_buf[FIXED_BUFFER_LEN];
    gchar *fail_pos;
    gdouble val;

    g_return_val_if_fail(nptr != NULL, 0);

    if (missingval) {
        gint len = strlen(missingval);

        p = nptr;
        while (g_ascii_isspace(*p))
            p++;

        if (strncmp(p, missingval, len) == 0) {
            if (endptr)
                *endptr = (gchar*)p + len;
            return NAN;
        }
    }

    fail_pos = NULL;

    decimal_point_pos = NULL;
    if (decimal_point[0] != ',' || decimal_point[1] != 0) {
        p = nptr;
        /* Skip leading space */
        while (g_ascii_isspace(*p))
            p++;

        /* Skip leading optional sign */
        if (*p == '+' || *p == '-')
            p++;

        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
            /* HEX - find the (optional) decimal point */
            while (g_ascii_isxdigit(*p))
                p++;

            if (*p == ',') {
                decimal_point_pos = p++;

                while (g_ascii_isxdigit(*p))
                    p++;
            }

            if (*p == 'p' || *p == 'P') {
                p++;
                if (*p == '+' || *p == '-')
                    p++;
                while (g_ascii_isdigit(*p))
                    p++;
            }
        }
        else {
            while (g_ascii_isdigit(*p))
                p++;

            if (*p == ',') {
                decimal_point_pos = p++;

                while (g_ascii_isdigit(*p))
                    p++;
            }

            if (*p == 'e' || *p == 'E') {
                p++;
                if (*p == '+' || *p == '-')
                    p++;
                while (g_ascii_isdigit(*p))
                    p++;
            }
        }
        /* For the other cases, we need not convert the decimal point */
        end = p;
    }

    /* Set errno to zero, so that we can distinguish zero results and underflows */
    errno = 0;

    if (decimal_point_pos) {
        /* The locale decimal point is not "," and we found ','. Convert it to the locale decimal point */
        gchar *copy, *c, *storage = NULL;
        guint buflen = end - nptr + 1 + decimal_point_len;

        c = copy = (buflen <= FIXED_BUFFER_LEN ? fixed_buf : (storage = g_malloc(buflen))); /* ?: is short-circuit */
        memcpy(c, nptr, decimal_point_pos - nptr);
        c += decimal_point_pos - nptr;
        memcpy(c, decimal_point, decimal_point_len);
        c += decimal_point_len;
        memcpy(c, decimal_point_pos + 1, end - (decimal_point_pos + 1));
        c += end - (decimal_point_pos + 1);
        *c = 0;

        val = strtod(copy, &fail_pos);

        if (fail_pos) {
            if (fail_pos - copy > decimal_point_pos - nptr)
                fail_pos = (gchar*)nptr + (fail_pos - copy) - (decimal_point_len - 1);
            else
                fail_pos = (gchar*)nptr + (fail_pos - copy);
        }

        if (storage)
            g_free(storage);
    }
    else {
        /* Otherwise either we did not find ',' or it is the locale decimal point so there is nothing to convert. */
        val = strtod(nptr, &fail_pos);
    }

    if (endptr)
        *endptr = fail_pos;

    return val;
}

static void
convert_legacy_exponent(GwyParams *params, gint id_exp, gint id_unit)
{
    gint power10_exp, power10_unit;
    GwySIValueFormat *vf;
    GwySIUnit *unit;

    if (!(power10_exp = gwy_params_get_int(params, id_exp)))
        return;

    unit = gwy_params_get_unit(params, id_unit, &power10_unit);
    vf = gwy_si_unit_get_format_for_power10(unit, GWY_SI_UNIT_FORMAT_PLAIN, power10_unit + power10_exp, NULL);
    gwy_params_set_unit(params, id_unit, vf->units);
    gwy_si_unit_value_format_free(vf);
    gwy_params_set_int(params, id_exp, 0);
}

static void
sanitise_params(GwyParams *params, gboolean full_module)
{
    RawFileBuiltin builtin = gwy_params_get_enum(params, PARAM_BUILTIN);
    gint size = (builtin ? builtin_size[builtin] : gwy_params_get_int(params, PARAM_SIZE));
    gboolean is_signed = (builtin == RAW_SIGNED_BYTE || builtin == RAW_SIGNED_WORD16
                          || builtin == RAW_SIGNED_WORD32 || builtin == RAW_SIGNED_WORD64);

    if (full_module) {
        gint xres = gwy_params_get_int(params, PARAM_XRES);
        gint yres = gwy_params_get_int(params, PARAM_YRES);
        gdouble xreal = gwy_params_get_double(params, PARAM_XREAL);
        gdouble yreal = gwy_params_get_double(params, PARAM_YREAL);
        const gchar *delim = gwy_params_get_string(params, PARAM_DELIMITER);

        if (xres != yres)
            gwy_params_set_boolean(params, PARAM_SQUARE_IMAGE, FALSE);
        if (fabs(log(fabs(xres/xreal*yreal/yres))) > EPS)
            gwy_params_set_boolean(params, PARAM_SQUARE_PIXELS, FALSE);
        gwy_params_set_enum(params, PARAM_DELIMITER_TYPE, delimiter_string_to_type(delim));
    }
    if (builtin) {
        gwy_params_set_int(params, PARAM_SIZE, size);
        gwy_params_set_int(params, PARAM_SKIP, gwy_params_get_int(params, PARAM_SKIP)/8*8);
        gwy_params_set_int(params, PARAM_ROWSKIP, gwy_params_get_int(params, PARAM_ROWSKIP)/8*8);
        gwy_params_set_boolean(params, PARAM_SIGN, is_signed);
        gwy_params_set_int(params, PARAM_BYTESWAP, MIN(gwy_params_get_int(params, PARAM_BYTESWAP), size/8-1));
    }
    else {
        gwy_params_set_int(params, PARAM_BYTESWAP, 0);
    }

    convert_legacy_exponent(params, PARAM_XYEXPONENT, PARAM_XYUNIT);
    convert_legacy_exponent(params, PARAM_ZEXPONENT, PARAM_ZUNIT);
}

static RawFileDelimiter
delimiter_string_to_type(const gchar *delim)
{
    if (gwy_strequal(delim, "\t"))
        return RAW_DELIM_TAB;
    if (gwy_strequal(delim, ""))
        return RAW_DELIM_WHITESPACE;
    return RAW_DELIM_OTHER;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
