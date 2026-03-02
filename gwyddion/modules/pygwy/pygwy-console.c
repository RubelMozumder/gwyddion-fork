/*
 *  $Id: pygwy-console.c 24708 2022-03-21 17:14:46Z yeti-dn $
 *  Copyright (C) 2008 Jan Horak
 *  E-mail: xhorak@gmail.com
 *  Copyright (C) 2014-2016 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 *  Description: This file contains pygwy console module.
 */

#include "config.h"
#include <Python.h>
#include <stdio.h>
#include <glib/gstdio.h>
#include <gdk/gdkkeysyms.h>
#include <libgwyddion/gwymacros.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#ifdef HAVE_GTKSOURCEVIEW
#include <gtksourceview/gtksourceview.h>
#include <gtksourceview/gtksourcelanguagemanager.h>
#endif

#include "pygwy-console.h"
#include "pygwy.h"

enum {
    NRECENT = 12
};

typedef struct {
   GtkWidget *window;
   PyObject *std_err;
   PyObject *dictionary;
   GtkWidget *console_output;
   GtkWidget *console_file_content;
   GtkToolItem *open_item;
   gchar *script_filename;
   GArray *recent_scripts;
} PygwyConsoleSetup;

static void       pygwy_console_open               (GtkToolButton *btn,
                                                    gpointer user_data);
static void       pygwy_console_save               (GtkToolButton *button,
                                                    gpointer user_data);
static void       pygwy_console_save_as            (GtkToolButton *button,
                                                    gpointer user_data);
static void       pygwy_console_run                (GtkToolButton *button,
                                                    gpointer user_data);
static void       pygwy_console                    (GwyContainer *data,
                                                    GwyRunType run,
                                                    const gchar *name);
static void       pygwy_console_command_execute    (GtkEntry *entry,
                                                    gpointer user_data);
static void       pygwy_console_clear_output       (GtkToolButton *btn,
                                                    gpointer user_data);
static void       pygwy_console_append_message     (const gchar *message);
static gboolean   pygwy_console_add_scriptfile     (const gchar *filename);
static void       pygwy_console_load_recent        (void);
static void       pygwy_console_save_recent        (void);
static void       pygwy_console_rebuild_recent_menu(void);
static GtkWidget* pygwy_console_create_recent_menu (void);
static void       pygwy_console_open_recent        (GtkMenuItem *item,
                                                    gpointer user_data);

static PygwyConsoleSetup *console_setup = NULL;

void
pygwy_register_console(void)
{
    gwy_process_func_register("pygwy_console",
                              pygwy_console,
                              N_("/Pygwy Console"),
                              GWY_STOCK_PYGWY,
                              GWY_RUN_IMMEDIATE,
                              0,
                              N_("Python wrapper console"));
}

static const char*
pygwy_console_run_command(const gchar *cmd, int mode)
{
    if (!cmd) {
        g_warning("No command.");
        return NULL;
    }

    if (!console_setup) {
        g_warning("Console setup structure is not defined!");
        return NULL;
    }
    /* store _pygwy_output_redir location */
    pygwy_run_string(cmd,
                     mode,
                     console_setup->dictionary,
                     console_setup->dictionary);
    pygwy_run_string(pygwy_stderr_redirect_readstr_code,
                     Py_file_input,
                     console_setup->dictionary,
                     console_setup->dictionary);

    return PyString_AsString(PyDict_GetItemString(console_setup->dictionary,
                                                  "_pygwy_stderr_string"));
}

static gboolean
key_pressed(GtkWidget *widget, GdkEventKey *event,
            G_GNUC_UNUSED PygwyConsoleSetup *setup)
{
    if (event->keyval != GDK_Escape
        || (event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK)))
        return FALSE;

    gtk_widget_hide(widget);
    return TRUE;
}

static void
pygwy_console_create_gui(void)
{
    GtkWidget *console_win, *vbox1, *console_scrolledwin, *file_scrolledwin,
              *vpaned, *frame, *icon;
    GtkWidget *entry_input, *button_bar;
    GtkToolItem *item;
    GtkTextView *file_textview, *output_textview;
    PangoFontDescription *font_desc;
    GtkAccelGroup *accel_group;
#ifdef HAVE_GTKSOURCEVIEW
    GtkSourceLanguageManager *manager;
    GtkSourceBuffer *sourcebuffer;
    GtkSourceLanguage *language;
#endif

    /* create static structure; */
    console_setup = g_new0(PygwyConsoleSetup, 1);
    console_setup->recent_scripts = g_array_new(FALSE, FALSE, sizeof(gchar*));
    pygwy_console_load_recent();

    /* create GUI */
    console_win = console_setup->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(console_win), _("Pygwy Console"));
    accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(console_win), accel_group);
    g_signal_connect(console_win, "key-press-event",
                     G_CALLBACK(key_pressed), console_setup);

    vbox1 = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(console_win), vbox1);

    /* Buttons. */
    button_bar = gtk_toolbar_new();
    gtk_box_pack_start(GTK_BOX(vbox1), button_bar, FALSE, FALSE, 0);
    gtk_toolbar_set_style(GTK_TOOLBAR(button_bar), GTK_TOOLBAR_BOTH);

    /* Open. */
    item = gtk_menu_tool_button_new_from_stock(GTK_STOCK_OPEN);
    console_setup->open_item = item;
    gtk_widget_set_tooltip_text(GTK_WIDGET(item),
                                _("Open script in Python language (Ctrl-O)"));
    gtk_widget_add_accelerator(GTK_WIDGET(item), "clicked", accel_group,
                               GDK_O, GDK_CONTROL_MASK,
                               GTK_ACCEL_VISIBLE);
    gtk_toolbar_insert(GTK_TOOLBAR(button_bar), item, -1);
    g_signal_connect(item, "clicked", G_CALLBACK(pygwy_console_open), NULL);

    /* Save. */
    item = gtk_tool_button_new_from_stock(GTK_STOCK_SAVE);
    gtk_widget_set_tooltip_text(GTK_WIDGET(item),
                                _("Save script (Ctrl-S)"));
    gtk_widget_add_accelerator(GTK_WIDGET(item), "clicked", accel_group,
                               GDK_S, GDK_CONTROL_MASK,
                               GTK_ACCEL_VISIBLE);
    gtk_toolbar_insert(GTK_TOOLBAR(button_bar), item, -1);
    g_signal_connect(item, "clicked", G_CALLBACK(pygwy_console_save), NULL);

    /* Save as. */
    item = gtk_tool_button_new_from_stock(GTK_STOCK_SAVE_AS);
    gtk_widget_set_tooltip_text(GTK_WIDGET(item),
                                _("Save script as (Ctrl-Shift-S)"));
    gtk_widget_add_accelerator(GTK_WIDGET(item), "clicked", accel_group,
                               GDK_S, GDK_CONTROL_MASK | GDK_SHIFT_MASK,
                               GTK_ACCEL_VISIBLE);
    gtk_toolbar_insert(GTK_TOOLBAR(button_bar), item, -1);
    g_signal_connect(item, "clicked", G_CALLBACK(pygwy_console_save_as), NULL);

    /* Run. */
    item = gtk_tool_button_new_from_stock(GTK_STOCK_EXECUTE);
    gtk_widget_set_tooltip_text(GTK_WIDGET(item),
                                _("Execute script (Ctrl-E)"));
    gtk_widget_add_accelerator(GTK_WIDGET(item), "clicked", accel_group,
                               GDK_E, GDK_CONTROL_MASK,
                               GTK_ACCEL_VISIBLE);
    gtk_toolbar_insert(GTK_TOOLBAR(button_bar), item, -1);
    g_signal_connect(item, "clicked", G_CALLBACK(pygwy_console_run), NULL);

    /* Clear. */
    icon = gtk_image_new_from_stock(GTK_STOCK_CLEAR,
                                    GTK_ICON_SIZE_LARGE_TOOLBAR);
    item = gtk_tool_button_new(icon, _("Clear Log"));
    gtk_toolbar_insert(GTK_TOOLBAR(button_bar), item, -1);
    g_signal_connect(item, "clicked",
                     G_CALLBACK(pygwy_console_clear_output), NULL);

    /* Text areas. */
    vpaned = gtk_vpaned_new();
    gtk_box_pack_start(GTK_BOX(vbox1), vpaned, TRUE, TRUE, 0);
    file_scrolledwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_paned_pack1(GTK_PANED(vpaned), file_scrolledwin, TRUE, FALSE);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(file_scrolledwin),
                                        GTK_SHADOW_IN);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(file_scrolledwin),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    console_scrolledwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(console_scrolledwin),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_paned_pack2(GTK_PANED(vpaned), console_scrolledwin, TRUE, TRUE);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(console_scrolledwin),
                                        GTK_SHADOW_IN);

    /* console output */
    console_setup->console_output = gtk_text_view_new();
    output_textview = GTK_TEXT_VIEW(console_setup->console_output);
    gtk_container_add(GTK_CONTAINER(console_scrolledwin),
                      console_setup->console_output);
    gtk_text_view_set_editable(output_textview, FALSE);

    /* file buffer */
#ifdef HAVE_GTKSOURCEVIEW
    console_setup->console_file_content = gtk_source_view_new();
    file_textview = GTK_TEXT_VIEW(console_setup->console_file_content);
    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(file_textview), TRUE);
    gtk_source_view_set_auto_indent(GTK_SOURCE_VIEW(file_textview), TRUE);
    manager = gtk_source_language_manager_get_default();

    sourcebuffer = GTK_SOURCE_BUFFER(gtk_text_view_get_buffer(file_textview));
    language = gtk_source_language_manager_get_language(manager, "pygwy");
    if (!language)
        language = gtk_source_language_manager_get_language(manager, "python");
    gtk_source_buffer_set_language(sourcebuffer, language);
    gtk_source_buffer_set_highlight_syntax(sourcebuffer, TRUE);

#else
    console_setup->console_file_content = gtk_text_view_new();
    file_textview = GTK_TEXT_VIEW(console_setup->console_file_content);
#endif
    /* set font */
    font_desc = pango_font_description_from_string("Monospace 8");
    gtk_widget_modify_font(console_setup->console_file_content, font_desc);
    gtk_widget_modify_font(console_setup->console_output, font_desc);
    pango_font_description_free(font_desc);

    gtk_container_add(GTK_CONTAINER(file_scrolledwin),
                      console_setup->console_file_content);
    gtk_text_view_set_editable(file_textview, TRUE);
    frame = gtk_frame_new(_("Command"));
    entry_input = gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(frame), entry_input);
    gtk_box_pack_start(GTK_BOX(vbox1), frame, FALSE, FALSE, 0);
    gtk_entry_set_invisible_char(GTK_ENTRY(entry_input), 9679);
    gtk_widget_grab_focus(GTK_WIDGET(entry_input));
    gtk_paned_set_position(GTK_PANED(vpaned), 300);

    /* entry widget on ENTER */
    g_signal_connect(entry_input, "activate",
                     G_CALLBACK(pygwy_console_command_execute), NULL);

    pygwy_console_rebuild_recent_menu();

    /* connect on window close() */
    g_signal_connect(console_win, "delete-event",
                     G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    gtk_text_view_set_wrap_mode(output_textview, GTK_WRAP_WORD_CHAR);
    gtk_window_resize(GTK_WINDOW(console_win), 600, 500);
    gtk_widget_show_all(console_win);
}

static void
pygwy_console(GwyContainer *data, GwyRunType run, const gchar *name)
{
    PyObject *d;

    if (console_setup && console_setup->window) {
        gtk_window_present(GTK_WINDOW(console_setup->window));
        return;
    }

    pygwy_initialize();
    pygwy_console_create_gui();
    console_setup->script_filename = NULL;
    /* create new environment */
    d = pygwy_create_environment("__console__", FALSE);
    if (!d) {
        g_warning("Cannot create copy of Python dictionary.");
        return;
    }

    /* redirect stdout & stderr to temporary file */
    pygwy_run_string(pygwy_stderr_redirect_setup_code
                     "import gwy\n"
                     "from gwy import *\n",
                     Py_file_input,
                     d,
                     d);

    /* store values for closing console */
    console_setup->std_err = PyDict_GetItemString(d, "_pygwy_output_redir");
    Py_INCREF(console_setup->std_err);
    console_setup->dictionary = d;
}

static void
pygwy_console_command_execute(GtkEntry *entry,
                              G_GNUC_UNUSED gpointer user_data)
{
    const gchar *command, *output;
    gchar *message;

    command = gtk_entry_get_text(entry);
    if (!strlen(command))
        return;

    output = pygwy_console_run_command(command, Py_single_input);
    message = g_strconcat(">>> ", command, "\n", output, NULL);
    pygwy_console_append_message(message);
    g_free(message);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
}

static void
pygwy_console_run(GtkToolButton *button, gpointer user_data)
{
    GtkTextView *textview;
    GtkTextBuffer *console_file_buf;
    GtkTextIter start_iter, end_iter;
    const gchar *output;
    gchar *script;

    textview = GTK_TEXT_VIEW(console_setup->console_file_content);
    console_file_buf = gtk_text_view_get_buffer(textview);

    pygwy_console_append_message(_(">>> Running the script above\n"));
    gtk_text_buffer_get_bounds(console_file_buf, &start_iter, &end_iter);
    script = gtk_text_buffer_get_text(console_file_buf,
                                      &start_iter, &end_iter, FALSE);
    output = pygwy_console_run_command(script, Py_file_input);
    g_free(script);
    pygwy_console_append_message(output);
}

static void
fix_eols_to_unix(gchar *text)
{
    gchar *p = strchr(text, '\r');
    guint i, j;

    /* Unix */
    if (!p)
        return;

    /* Mac */
    if (p[1] != '\n') {
        do {
            *p = '\n';
        } while ((p = strchr(p+1, '\r')));

        return;
    }

    /* MS-DOS */
    for (i = 0, j = 0; text[i]; i++) {
        if (text[i] != '\r') {
            text[j] = text[i];
            j++;
        }
    }
    text[j] = '\0';
}

/* The file chooser dialogue cannot be meaningfully switched between modes
 * (button labels stays saying Open/Save, ...).  So having two means we need
 * to sync them. */
static GtkFileChooser*
ensure_pygwy_file_dialogue(GtkFileChooserAction action, const gchar *filename)
{
    static GtkFileChooser *open_chooser = NULL;
    static GtkFileChooser *save_chooser = NULL;

    GtkWidget *widget;
    GtkFileChooser *chooser;
    gboolean is_save = (action == GTK_FILE_CHOOSER_ACTION_SAVE);
    const gchar *title;

    if (is_save) {
        chooser = save_chooser;
        title = _("Save Python Script as");
    }
    else {
        chooser = open_chooser;
        title = _("Open Python Script");
    }

    if (!chooser) {
        GtkFileFilter *filter = gtk_file_filter_new();

        gtk_file_filter_add_mime_type(filter, "text/x-python");
        gtk_file_filter_add_pattern(filter, "*.py");

        widget = gtk_file_chooser_dialog_new(title, NULL, action,
                                             GTK_STOCK_CANCEL,
                                             GTK_RESPONSE_CANCEL,
                                             GTK_STOCK_OPEN,
                                             GTK_RESPONSE_ACCEPT,
                                             NULL);
        chooser = GTK_FILE_CHOOSER(widget);
        gtk_file_chooser_set_filter(chooser, filter);
        g_signal_connect(widget, "delete-event",
                         G_CALLBACK(gtk_widget_hide_on_delete), NULL);

        if (is_save) {
            gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
            save_chooser = chooser;
        }
        else
            open_chooser = chooser;
    }

    /* Sync the state between open and save choosers. */
    if (filename)
        gtk_file_chooser_set_filename(chooser, filename);

    return chooser;
}

static void
update_script_filename(const gchar *filename)
{
    gchar *title, *basename;

    if (console_setup->script_filename
        && gwy_strequal(filename, console_setup->script_filename))
        return;

    g_free(console_setup->script_filename);
    console_setup->script_filename = g_strdup(filename);
    if (pygwy_console_add_scriptfile(filename)) {
        pygwy_console_save_recent();
        pygwy_console_rebuild_recent_menu();
    }

    basename = g_path_get_basename(filename);
    title = g_strconcat(_("Pygwy Console"), " – ", basename, NULL);
    gtk_window_set_title(GTK_WINDOW(console_setup->window), title);
    g_free(basename);
    g_free(title);
}

static void
pygwy_console_load_script(const gchar *filename)
{
    GtkTextBuffer *console_file_buf;
    GtkTextView *textview;
    gchar *file_content, *message;
    GError *err = NULL;

    if (g_file_get_contents(filename, &file_content, NULL, &err)) {
        fix_eols_to_unix(file_content);
        textview = GTK_TEXT_VIEW(console_setup->console_file_content);
        console_file_buf = gtk_text_view_get_buffer(textview);
        gtk_text_buffer_set_text(console_file_buf, file_content, -1);
        g_free(file_content);
        update_script_filename(filename);
    }
    else {
        message = g_strdup_printf(_("Cannot read from file: %s."), err->message);
        pygwy_console_append_message(message);
        g_clear_error(&err);
        g_free(message);
    }
}

static void
pygwy_console_save_script(const gchar *filename)
{
    GtkTextView *textview;
    GtkTextBuffer *buf;
    GtkTextIter start_iter, end_iter;
    gchar *script, *message;
    GError *err = NULL;

    textview = GTK_TEXT_VIEW(console_setup->console_file_content);
    buf = gtk_text_view_get_buffer(textview);
    gtk_text_buffer_get_bounds(buf, &start_iter, &end_iter);
    script = gtk_text_buffer_get_text(buf, &start_iter, &end_iter, FALSE);
    if (g_file_set_contents(filename, script, -1, &err)) {
        update_script_filename(filename);
    }
    else {
        message = g_strdup_printf(_("Cannot write to file: %s."), err->message);
        pygwy_console_append_message(message);
        g_clear_error(&err);
        g_free(message);
    }
    g_free(script);
}

static void
pygwy_console_open(GtkToolButton *button, gpointer user_data)
{
    GtkFileChooser *chooser;
    gchar *filename;
    gboolean ok;

    chooser = ensure_pygwy_file_dialogue(GTK_FILE_CHOOSER_ACTION_OPEN,
                                         console_setup->script_filename);
    gtk_window_present(GTK_WINDOW(chooser));
    ok = (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT);
    gtk_widget_hide(GTK_WIDGET(chooser));
    if (ok) {
        filename = gtk_file_chooser_get_filename(chooser);
        pygwy_console_load_script(filename);
        g_free(filename);
    }
}

static void
pygwy_console_save(GtkToolButton *button, gpointer user_data)
{
    if (!console_setup->script_filename) 
        pygwy_console_save_as(button, user_data);
    else
        pygwy_console_save_script(console_setup->script_filename);
}

static void
pygwy_console_save_as(GtkToolButton *button, gpointer user_data)
{
    GtkFileChooser *chooser;
    gchar *filename;
    gboolean ok;

    chooser = ensure_pygwy_file_dialogue(GTK_FILE_CHOOSER_ACTION_SAVE,
                                         console_setup->script_filename);
    gtk_window_present(GTK_WINDOW(chooser));
    ok = (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT);
    gtk_widget_hide(GTK_WIDGET(chooser));
    if (ok) {
        filename = gtk_file_chooser_get_filename(chooser);
        pygwy_console_save_script(filename);
        g_free(filename);
    }
}

static gboolean
pygwy_console_add_scriptfile(const gchar *filename)
{
    GArray *recent = console_setup->recent_scripts;
    gchar *rfilename;
    guint i;

    for (i = 0; i < recent->len; i++) {
        rfilename = g_array_index(recent, gchar*, i);
        if (gwy_strequal(filename, rfilename)) {
            if (i == 0)
                return FALSE;

            /* Move it to the front. */
            g_array_remove_index(recent, i);
            g_array_prepend_val(recent, rfilename);
            return TRUE;
        }
    }

    rfilename = g_strdup(filename);
    g_array_prepend_val(recent, rfilename);
    if (recent->len > NRECENT)
        g_array_set_size(recent, NRECENT);
    return TRUE;
}

static void
pygwy_console_load_recent(void)
{
    GArray *recent = console_setup->recent_scripts;
    gchar *text, *line, *p;

    if (!gwy_module_data_load("pygwy_console", "recent-files",
                              &text, NULL, NULL))
        return;

    p = text;
    for (line = gwy_str_next_line(&p); line; line = gwy_str_next_line(&p)) {
        if (strchr(line, '\\')) {
            line = g_strcompress(line);
            if (strlen(line))
                pygwy_console_add_scriptfile(line);
            g_free(line);
        }
        else if (strlen(line))
            pygwy_console_add_scriptfile(line);

        if (recent->len == NRECENT)
            break;
    }
    g_free(text);
}

static void
pygwy_console_save_recent(void)
{
    GArray *recent = console_setup->recent_scripts;
    GString *text = g_string_new(NULL);
    gchar *line;
    guint i;

    for (i = 0; i < recent->len; i++) {
        /* Put the most recent last; it will end up first. */
        line = g_strescape(g_array_index(recent, gchar*, recent->len-1 - i),
                           NULL);
        g_string_append(text, line);
        g_string_append_c(text, '\n');
    }

    gwy_module_data_save("pygwy_console", "recent-files", text->str, -1, NULL);
    g_string_free(text, TRUE);
}

static void
pygwy_console_rebuild_recent_menu(void)
{
    GtkToolItem *item = console_setup->open_item;
    GtkWidget *menu;

    g_return_if_fail(item);
    menu = gtk_menu_tool_button_get_menu(GTK_MENU_TOOL_BUTTON(item));
    if (menu) {
        gtk_widget_destroy(menu);
        g_object_unref(menu);
    }
    menu = pygwy_console_create_recent_menu();
    g_object_ref(menu);
    gtk_widget_show_all(menu);
    gtk_menu_tool_button_set_menu(GTK_MENU_TOOL_BUTTON(item), menu);
}

static GtkWidget*
pygwy_console_create_recent_menu(void)
{
    GArray *recent = console_setup->recent_scripts;
    GtkWidget *menu, *item;
    GtkMenuShell *menushell;
    const gchar *path;
    gchar *filename;
    guint i;

    menu = gtk_menu_new();
    menushell = GTK_MENU_SHELL(menu);
    for (i = 0; i < recent->len; i++) {
        path = g_array_index(recent, gchar*, i);
        filename = g_path_get_basename(path);
        item = gtk_menu_item_new_with_label(filename);
        g_free(filename);
        gtk_menu_shell_append(menushell, item);
        g_signal_connect(item, "activate",
                         G_CALLBACK(pygwy_console_open_recent),
                         GUINT_TO_POINTER(i));
    }
    return menu;
}

static void
pygwy_console_open_recent(GtkMenuItem *item, gpointer user_data)
{
    GArray *recent = console_setup->recent_scripts;
    guint i = GPOINTER_TO_UINT(user_data);

    g_return_if_fail(i < recent->len);
    pygwy_console_load_script(g_array_index(recent, gchar*, i));
}

static void
pygwy_console_clear_output(GtkToolButton *btn,
                           G_GNUC_UNUSED gpointer user_data)
{
    GtkTextBuffer *console_buf;
    GtkTextIter start_iter, end_iter;
    GtkTextView *textview;

    textview = GTK_TEXT_VIEW(console_setup->console_output);
    console_buf = gtk_text_view_get_buffer(textview);
    gtk_text_buffer_get_bounds(console_buf, &start_iter, &end_iter);
    gtk_text_buffer_delete(console_buf, &start_iter, &end_iter);
}

static void
pygwy_console_append_message(const gchar *message)
{
    GtkTextBuffer *console_buf;
    GtkTextIter start_iter, end_iter;
    GtkTextView *textview;
    GString *output;
    GtkTextMark *end_mark;

    if (!message) {
        g_warning("No message to append.");
        return;
    }
    if (!console_setup) {
        g_warning("Console setup structure is not defined!");
        return;
    }
    /* read string which contain last command output */
    textview = GTK_TEXT_VIEW(console_setup->console_output);
    console_buf = gtk_text_view_get_buffer(textview);
    gtk_text_buffer_get_bounds(console_buf, &start_iter, &end_iter);

    /* get output widget content */
    output = g_string_new(gtk_text_buffer_get_text(console_buf,
                                                   &start_iter, &end_iter,
                                                   FALSE));

    /* append input line */
    output = g_string_append(output, message);
    gtk_text_buffer_set_text(console_buf, output->str, -1);
    g_string_free(output, TRUE);

    /* scroll to end */
    gtk_text_buffer_get_end_iter(console_buf, &end_iter);
    end_mark = gtk_text_buffer_create_mark(console_buf, "cursor", &end_iter,
                                           FALSE);
    g_object_ref(end_mark);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(console_setup->console_output),
                                 end_mark, 0.0, FALSE, 0.0, 0.0);
    g_object_unref(end_mark);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
