/*
 *  $Id: pygwy.c 22841 2020-06-16 12:04:36Z yeti-dn $
 *  Copyright (C) 2004-2017 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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
 */

/* Only one interpreter is created. After initialization of '__main__'
 * and 'gwy' module the directory is copied every time the independent
 * pseudo-sub-interpreter is needed. So every module is called with
 * own copy of main dictionary created by create_environment() function
 * and destroyed by destroy_environment() which deallocate created copy.
 */
#include "config.h"

/* include this first, before NO_IMPORT_PYGOBJECT is defined */
#include <pygobject.h>
#include "wrap_calls.h"
#include <stdarg.h>
#include <glib/gstdio.h>
#include <Python.h>

#include "pygwy.h"

#include "pygwywrap.c"
#include "pygwy-console.h"
#line 41 "pygwy.c"

#ifdef G_OS_WIN32
#include <windows.h>
#include <winreg.h>
#define python_version "2.7"
#define python_key "Software\\Python\\PythonCore\\" python_version "\\InstallPath"
#endif

typedef enum {
    PYGWY_UNDEFINED = -1,
    PYGWY_PROCESS,
    PYGWY_FILE,
    PYGWY_GRAPH,
    PYGWY_LAYER,
    PYGWY_TOOL,
    PYGWY_VOLUME,
    PYGWY_XYZ,
} PygwyModuleType;

typedef struct {
    gchar *name;
    gchar *fullpath;
    PyObject *code;
    time_t m_time;
    PygwyModuleType type;

    /* Used only during registration and may become dangling pointers
     * afterwards. */
    const gchar *desc;
    const gchar *menu_path;
    const gchar *icon;
    GwyMenuSensFlags sens;
    GwyRunType run;

    /* Used dynamically during execution */
    PyObject *module;
    PyObject *dict;
    PyObject *func;
} PygwyModuleInfo;

static gboolean         module_register               (void);
static gboolean         check_pygtk_availability      (void);
static void             pygwy_procvolxyz_run          (GwyContainer *data,
                                                       GwyRunType run,
                                                       const gchar *name);
static void             pygwy_graph_run               (GwyGraph *graph,
                                                       const gchar *name);
static gboolean         pygwy_file_save_run           (GwyContainer *data,
                                                       const gchar *filename,
                                                       GwyRunType mode,
                                                       GError **error,
                                                       const gchar *name);
static GwyContainer*    pygwy_file_load_run           (const gchar *filename,
                                                       GwyRunType mode,
                                                       GError **error,
                                                       const gchar *name);
static gint             pygwy_file_detect_run         (const GwyFileDetectInfo *fileinfo,
                                                       gboolean only_name,
                                                       const gchar *name);
static void             pygwy_register_modules        (void);
static gchar**          find_module_candidates        (gchar **module_dir_name);
static void             free_module_info              (PygwyModuleInfo *info);
static gboolean         update_module_code            (PygwyModuleInfo *info);
static PygwyModuleInfo* pygwy_find_module             (const gchar* name);
static PygwyModuleInfo* prepare_to_run_module_function(const gchar *name,
                                                       const gchar *funcname);
static void             finalize_module_function      (PygwyModuleInfo *info);
static PyObject*        get_attribute_recursive       (PyObject *obj,
                                                       ...);
static gint             find_out_number_of_arguments  (PyObject *func);
static gchar*           find_out_class_name           (PyObject *obj);

static PyObject *pygwy_module = NULL;
static PyObject *pygwy_dict = NULL;
static GList *registered_modules = NULL;

static const GwyEnum module_types[] = {
    { "PROCESS", PYGWY_PROCESS, },
    { "FILE",    PYGWY_FILE,    },
    { "GRAPH",   PYGWY_GRAPH,   },
/*    { "LAYER",   PYGWY_LAYER,   }, */
/*    { "TOOL",    PYGWY_TOOL,    }, */
    { "VOLUME",  PYGWY_VOLUME,  },
    { "XYZ",     PYGWY_XYZ,     },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Pygwy, the Gwyddion Python wrapper."),
    "Jan Hořák <xhorak@gmail.com>, Yeti <yeti@gwyddion.net>",
    "2.6",
    "Jan Hořák & David Nečas (Yeti)",
    "2007"
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    if (!check_pygtk_availability())
        return FALSE;

    pygwy_register_modules();
    pygwy_register_console();
    return TRUE;
}

/* If python or pygtk is not available it crashes or worse.  Try to figure out
 * whether it is a good idea to register the module function or not. */
static gboolean
check_pygtk_availability(void)
{
#ifdef G_OS_WIN32
    gchar pythondir[256];
    DWORD size = sizeof(pythondir)-1;
    HKEY reg_key;
    gchar *filename;
    gboolean ok = FALSE;

    gwy_clear(pythondir, sizeof(pythondir));
    if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT(python_key), 0, KEY_READ,
                     &reg_key) == ERROR_SUCCESS) {
        if (RegQueryValueEx(reg_key, NULL, NULL, NULL, pythondir, &size) == ERROR_SUCCESS) {
            ok = TRUE;
        }
        RegCloseKey(reg_key);
    }
    if (!ok
        && RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT(python_key), 0, KEY_READ,
                        &reg_key) == ERROR_SUCCESS) {
        if (RegQueryValueEx(reg_key, NULL, NULL, NULL,
                            pythondir, &size) == ERROR_SUCCESS)
            ok = TRUE;
        RegCloseKey(reg_key);
    }

    if (!ok) {
        g_message("Cannot get %s registry key, assuming no python 2.7.",
                  python_key);
        return FALSE;
    }

    gwy_debug("python path %s", pythondir);
    filename = g_build_filename(pythondir, "Lib", "site-packages", "gtk-2.0",
                                "gobject", "__init__.py", NULL);
    if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        g_message("File %s is not present, assuming no pygobject.", filename);
        g_free(filename);
        return FALSE;
    }
    g_free(filename);

    filename = g_build_filename(pythondir, "Lib", "site-packages", "gtk-2.0",
                                "gtk", "__init__.py", NULL);
    if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        g_message("File %s is not present, assuming no pygtk.", filename);
        g_free(filename);
        return FALSE;
    }
    g_free(filename);

    filename = g_build_filename(pythondir, "Lib", "site-packages", "cairo",
                                "__init__.py", NULL);
    if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR)) {
        g_message("File %s is not present, assuming no pycairo.", filename);
        g_free(filename);
        return FALSE;
    }
    g_free(filename);
#endif

    return TRUE;
}

static void
check_duplicit_wrappers(const PyMethodDef* func)
{
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    while (func->ml_name) {
        if (g_hash_table_lookup(seen, (gpointer)func->ml_name))
            g_warning("Duplicit pygwy function %s.", func->ml_name);
        else
            g_hash_table_insert(seen, (gpointer)func->ml_name,
                                GUINT_TO_POINTER(TRUE));
        func++;
    }
    g_hash_table_destroy(seen);
}

void
pygwy_initialize(void)
{
    PyObject *m;

    if (!Py_IsInitialized()) {
        gwy_debug("Checking function table sanity");
        check_duplicit_wrappers(pygwy_functions);

        gwy_debug("Initializing Python interpreter");
        /* Do not register signal handlers */
        Py_InitializeEx(0);
        gwy_debug("Add main module");
        pygwy_module = PyImport_AddModule("__main__");
        gwy_debug("Init pygobject");
        init_pygobject();

        gwy_debug("Init module gwy");
        m = Py_InitModule("gwy", (PyMethodDef*)pygwy_functions);
        gwy_debug("gwy module = %p", m);
        gwy_debug("Get dict");
        pygwy_dict = PyModule_GetDict(m);
        gwy_debug("dict = %p", pygwy_dict);

        gwy_debug("Register classes");
        pygwy_register_classes(pygwy_dict);
        gwy_debug("Register constaints");
        pygwy_add_constants(m, "GWY_");
    }
    else {
        gwy_debug("Python interpreter already initialized");
    }
}

static void
pygwy_show_stderr(gchar *str)
{
    GtkWidget *dlg, *scroll, *text;

    dlg = gtk_dialog_new_with_buttons(_("Python Interpreter Errors"), NULL, 0,
                                      GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                      NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 600, 350);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), scroll, TRUE, TRUE, 0);

    text = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(scroll), text);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text)),
                             str,
                             -1);

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void
pygwy_initialize_stderr_redirect(PyObject *d)
{
    /* redirect stderr to temporary file */
    pygwy_run_string(pygwy_stderr_redirect_setup_code, Py_file_input, d, d);
}

/* rewind redirected stderr file, read its content and display it in error
 * window */
static void
pygwy_finalize_stderr_redirect(PyObject *d)
{
    PyObject *py_stderr;
    gchar *buf;

    pygwy_run_string(pygwy_stderr_redirect_restore_code, Py_file_input, d, d);
    py_stderr = PyDict_GetItemString(d, "_pygwy_stderr_string");
    if (py_stderr && PyString_Check(py_stderr)) {
        buf = PyString_AsString(py_stderr);
        gwy_debug("Pygwy module stderr output:\n%s", buf);
        /* show stderr only when it is not empty string */
        if (buf[0] != '\0')
            pygwy_show_stderr(buf);
    }
}

static void
pygwy_add_sys_path(PyObject *dir, const gchar *path)
{
    static const gchar sys_path_append_template[] =
        "import sys\n"
        "if '%s' not in sys.path:\n"
        "    sys.path.append('%s')\n"
        "\n";

    if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        gchar *p = gwy_strreplace(path, "'", "\\'", (gsize)-1);
        gchar *command = g_strdup_printf(sys_path_append_template, p, p);
        g_free(p);
        pygwy_run_string(command, Py_file_input, dir, dir);
        g_free(command);
    }
    else {
        g_warning("Cannot add non-existent path '%s'.", path);
    }
}

static void
augment_sys_path(PyObject *d)
{
    const gchar *userdir;
    gchar *module_dir_name, *datadir;

    /* add ~/.gwyddion/pygwy to sys.path */
    userdir = gwy_get_user_dir();
    module_dir_name = g_build_filename(userdir, pygwy_module_dir_name, NULL);
    pygwy_add_sys_path(d, module_dir_name);
    g_free(module_dir_name);

    /* add /usr/local/share/gwyddion/pygwy to sys.path */
    datadir = gwy_find_self_dir("data");
    module_dir_name = g_build_filename(datadir, pygwy_module_dir_name, NULL);
    pygwy_add_sys_path(d, module_dir_name);
    g_free(module_dir_name);
}

PyObject*
pygwy_create_environment(const gchar *filename, gboolean show_errors)
{
    PyObject *d, *module_filename;
    char *argv[1];
    argv[0] = NULL;

    d = PyDict_Copy(PyModule_GetDict(pygwy_module));
    gwy_debug("copying dict from gwy %p as %p", pygwy_module, d);
    if (!d) {
        g_warning("Cannot create copy of Python dictionary.");
        return NULL;
    }

    /* set __file__ variable for clearer error reporting */
    module_filename = Py_BuildValue("s", filename);
    PyDict_SetItemString(d, "__file__", module_filename);
    PySys_SetArgv(0, argv);
    Py_DECREF(module_filename);

    /* redirect stderr and stdout of python script to temporary file */
    if (show_errors)
        pygwy_initialize_stderr_redirect(d);

    augment_sys_path(d);
    return d;
}

/* show content of temporary file which contains stderr and stdout of python 
 * script and close it */
static void
destroy_environment(PyObject *d, gboolean show_errors)
{
    if (!d)
        return;
    if (show_errors)
        pygwy_finalize_stderr_redirect(d);
    PyDict_Clear(d);
    Py_DECREF(d);
}

static void
err_PYTHON(GError **error)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                _("Python interpreter error occurred."));
}

static const gchar*
pygwy_read_str_from_dict(PyObject *dict, char *v, const gchar *filename,
                         gboolean required)
{
    PyObject *py_str;
    const char *ret;

    if ((py_str = PyDict_GetItemString(dict, v))) {
        if (PyArg_Parse(py_str, "s", &ret)) {
            gwy_debug("Read value '%s' from '%s': %s", v, filename, ret);
            return ret;
        }
    }
    if (required) {
        g_warning("Value '%s' not found in '%s'.", v, filename);
    }
    return NULL;
}

static gboolean
pygwy_read_flags_from_dict(PyObject *dict, char *v, const gchar *filename,
                           guint *ret)
{
    PyObject *py_flags;

    if ((py_flags = PyDict_GetItemString(dict, v))) {
        if (PyArg_Parse(py_flags, "I", ret)) {
            gwy_debug("Read value '%s' from '%s': %u", v, filename, *ret);
            return TRUE;
        }
    }
    return FALSE;
}

/* Contract: It must either return NULL or PygwyModuleInfo that is correct
 * for given module type with all required fields filled. */
static PygwyModuleInfo*
pygwy_get_module_info(const gchar *fullpath, const gchar *filename)
{
    PygwyModuleInfo *info = NULL;
    gchar *suffix;
    const gchar *type_str;
    PyObject *module = NULL, *d, *module_dict;
    gboolean type_is_dataprocess;

    info = g_new0(PygwyModuleInfo, 1);
    info->fullpath = g_strdup(fullpath);
    info->type = PYGWY_UNDEFINED;

    info->name = g_path_get_basename(fullpath);
    suffix = g_strrstr(info->name, ".");
    if (suffix)
        *suffix = '\0';
    gwy_debug("module name: %s", info->name);

    if (!(d = pygwy_create_environment(fullpath, TRUE)))
        goto fail;

    if (!update_module_code(info))
        goto fail;

    /* Execute compiled module */
    module = PyImport_ExecCodeModule("get_data", info->code); /* new ref */
    if (!module) {
        g_warning("Cannot exec module code in file '%s'", fullpath);
        goto fail;
    }

    /* Get parameters from dict */
    module_dict = PyModule_GetDict(module);
    type_str = pygwy_read_str_from_dict(module_dict, "plugin_type", fullpath,
                                        TRUE);
    gwy_debug("read values: %s %p", type_str, type_str);
    if (!type_str) {
        g_warning("Undefined module type, cannot load.");
        goto fail;
    }
    info->type = gwy_string_to_enum(type_str,
                                    module_types, G_N_ELEMENTS(module_types));
    if (info->type == PYGWY_UNDEFINED) {
        g_warning("Unrecognised module type %s, cannot load.", type_str);
        goto fail;
    }
    type_is_dataprocess = (info->type == PYGWY_PROCESS
                           || info->type == PYGWY_GRAPH
                           || info->type == PYGWY_VOLUME
                           || info->type == PYGWY_XYZ);

    /* plugin_desc is required for file modules (as the file type) */
    info->desc = pygwy_read_str_from_dict(module_dict, "plugin_desc", fullpath,
                                          TRUE);
    if (info->type == PYGWY_FILE && !info->desc) {
        info->type = PYGWY_UNDEFINED;
        goto fail;
    }
    else if (!info->desc) {
        /* Not very descriptive... */
        info->desc = N_("Function written in Python");
    }
    gwy_debug("desc: %s", info->desc);

    /* menu path is required for all something-processing modules */
    if (type_is_dataprocess) {
        info->menu_path = pygwy_read_str_from_dict(module_dict, "plugin_menu",
                                                   fullpath, TRUE);
        if (!info->menu_path) {
            info->type = PYGWY_UNDEFINED;
            goto fail;
        }
        gwy_debug("menu_path: %s", info->menu_path);
    }

    /* icon is optional for something-processing modules */
    if (type_is_dataprocess) {
        info->icon = pygwy_read_str_from_dict(module_dict, "plugin_icon",
                                              fullpath, FALSE);
        gwy_debug("icon: %s", info->icon);
    }

    /* run mode and sensitivity are optional for something-processing modules */
    if (type_is_dataprocess) {
        if (info->type == PYGWY_PROCESS)
            info->sens = GWY_MENU_FLAG_DATA;
        else if (info->type == PYGWY_GRAPH)
            info->sens = GWY_MENU_FLAG_GRAPH;
        else if (info->type == PYGWY_VOLUME)
            info->sens = GWY_MENU_FLAG_VOLUME;
        else if (info->type == PYGWY_XYZ)
            info->sens = GWY_MENU_FLAG_XYZ;

        pygwy_read_flags_from_dict(module_dict, "plugin_sens", fullpath,
                                   &info->sens);
        gwy_debug("sens: 0x%04x", info->sens);

        info->run = (GWY_RUN_INTERACTIVE | GWY_RUN_IMMEDIATE);
        pygwy_read_flags_from_dict(module_dict, "plugin_run", fullpath,
                                   &info->run);
        gwy_debug("run: 0x%04x", info->run);
    }

fail:
    if (info->type == PYGWY_UNDEFINED) {
        PyErr_Print();
        free_module_info(info);
        info = NULL;
    }
    Py_XDECREF(module);
    destroy_environment(d, TRUE);

    return info;
}

static void
free_module_info(PygwyModuleInfo *info)
{
    if (!info)
        return;

    g_free(info->name);
    g_free(info->fullpath);
    Py_XDECREF(info->code);
    g_free(info);
}

static void
pygwy_register_modules(void)
{
    gchar *fullpath, *module_dir_name;
    PygwyModuleInfo *info;
    gboolean ok;
    gchar **candidates;
    guint i;

    candidates = find_module_candidates(&module_dir_name);
    if (!candidates) {
        g_free(module_dir_name);
        return;
    }

    /* Initialize the python interpret and init gwy module.  Only do it here
     * if we find any potential python modules.  Otherwise postpone the cost
     * to pygwy console invocation, if if ever comes. */
    pygwy_initialize();
    for (i = 0; candidates[i]; i++) {
        /* Read content of module file */
        fullpath = g_build_filename(module_dir_name, candidates[i], NULL);

        /* get module's metadata */
        info = pygwy_get_module_info(fullpath, candidates[i]);
        g_free(fullpath);
        if (!info)
            continue;

        gwy_debug("module type: %d", info->type);
        if (info->type == PYGWY_PROCESS) {
            gwy_debug("Registering proc func.");
            ok = gwy_process_func_register(info->name, pygwy_procvolxyz_run,
                                           info->menu_path, info->icon,
                                           info->run, info->sens, info->desc);
        }
        else if (info->type == PYGWY_FILE) {
            gwy_debug("Registering file func.");
            ok = gwy_file_func_register(info->name, info->desc,
                                        pygwy_file_detect_run,
                                        pygwy_file_load_run,
                                        NULL,
                                        pygwy_file_save_run);
        }
        else if (info->type == PYGWY_GRAPH) {
            gwy_debug("Registering graph func.");
            ok = gwy_graph_func_register(info->name, pygwy_graph_run,
                                         info->menu_path, info->icon,
                                         info->sens, info->desc);
        }
        else if (info->type == PYGWY_VOLUME) {
            gwy_debug("Registering volume func.");
            ok = gwy_volume_func_register(info->name, pygwy_procvolxyz_run,
                                          info->menu_path, info->icon,
                                          info->run, info->sens, info->desc);
        }
        else if (info->type == PYGWY_XYZ) {
            gwy_debug("Registering xyz func.");
            ok = gwy_volume_func_register(info->name,
                                          pygwy_procvolxyz_run,
                                          info->menu_path, info->icon,
                                          info->run, info->sens, info->desc);
        }
        else {
            g_assert_not_reached();
        }

        if (ok)
            registered_modules = g_list_append(registered_modules, info);
        else
            free_module_info(info);
    }
    g_free(module_dir_name);
    g_strfreev(candidates);
}

static gchar**
find_module_candidates(gchar **module_dir_name)
{
    GDir *module_dir;
    const gchar *filename, *dot;
    GPtrArray *candidates = NULL;
    GError *err = NULL;

    *module_dir_name = g_build_filename(gwy_get_user_dir(),
                                        pygwy_module_dir_name,
                                        NULL);
    gwy_debug("Module path: %s", *module_dir_name);

    module_dir = g_dir_open(*module_dir_name, 0, &err);
    if (module_dir == NULL && err) {
        if (err->code == G_FILE_ERROR_NOENT) {
            /* directory not found/does not exist */
            if (g_mkdir(*module_dir_name, 0700)) {
                g_warning("Cannot create pygwy module directory %s",
                          *module_dir_name);
            }
            else {
                gwy_debug("Pygwy directory created: %s", *module_dir_name);
            }
        }
        else {
            g_warning("Cannot open pygwy directory: %s, reason: %s",
                      *module_dir_name, err->message);
        }
        /* Whether the directory has been created or not, there is no reason
           to continue by reading scripts as long as no script is available */
        return NULL;
    }

    while ((filename = g_dir_read_name(module_dir))) {
        if (!(dot = strrchr(filename, '.'))) {
            gwy_debug("Skipping file with no extension: %s", filename);
            continue;
        }
        if (g_ascii_strcasecmp(dot+1, "py") != 0) {
            gwy_debug("Skipping file with wrong extension: %s", filename);
            continue;
        }

        if (!candidates)
            candidates = g_ptr_array_new();
        g_ptr_array_add(candidates, g_strdup(filename));
    }
    g_dir_close(module_dir);

    if (!candidates)
        return NULL;

    g_ptr_array_add(candidates, NULL);
    return (gchar**)g_ptr_array_free(candidates, FALSE);
}

/* (Re)load module code.  The function succeeds if, after finishing, we have
 * any module code at all.  So when the module has been loaded once but then
 * cannot be reloaded we just keep the old version and succeed. */
static gboolean
update_module_code(PygwyModuleInfo *info)
{
    struct stat file_stat;
    gchar *module_file_content, *s;
    PyObject *code_obj;
    GError *err;

    gwy_debug("Updating module code from '%s'", info->fullpath);
    if (g_stat(info->fullpath, &file_stat)) {
        g_warning("Cannot get last modification time for file '%s'",
                  info->fullpath);
        return !!info->code;
    }

    if (info->code && file_stat.st_mtime == info->m_time) {
        gwy_debug("No changes in '%s' since last run.", info->fullpath);
        return TRUE;
    }

    gwy_debug("File '%s' has to be loaded.", info->fullpath);
    if (!g_file_get_contents(info->fullpath, &module_file_content, NULL,
                             &err)) {
        g_warning("Cannot read content of file '%s'", info->fullpath);
        return !!info->code;
    }

    /* Ensure gwy is always available as we promised. */
    s = g_strconcat("import gwy\n\n", module_file_content, NULL);
    g_free(module_file_content);
    module_file_content = s;

    code_obj = Py_CompileString(module_file_content, info->name, Py_file_input);
    if (!code_obj) {
        g_free(module_file_content);
        g_warning("Cannot create code object for file '%s'", info->fullpath);
        PyErr_Print();
        return !!info->code;
    }

    g_free(module_file_content);
    GWY_SWAP(PyObject*, info->code, code_obj);
    Py_XDECREF(code_obj);
    info->m_time = file_stat.st_mtime;
    return TRUE;
}

static void
pygwy_procvolxyz_run(GwyContainer *data, GwyRunType run, const gchar *name)
{
    PygwyModuleInfo *info;
    PyObject *py_container = NULL, *res = NULL;
    gint nargs;

    if (!(info = prepare_to_run_module_function(name, "run")))
        return;
    py_container = pygobject_new((GObject*)data);
    nargs = find_out_number_of_arguments(info->func);
    if (nargs == 0) {
        /* Legacy API; module expects a global variable "data" */
        PyDict_SetItemString(pygwy_dict, "data", py_container);
        res = PyObject_CallFunction(info->func, NULL);
        PyDict_DelItemString(pygwy_dict, "data");
    }
    else if (nargs == 1)
        res = PyObject_CallFunction(info->func, "O", py_container);
    else if (nargs == 2)
        res = PyObject_CallFunction(info->func, "Oi", py_container, run);
    else {
        g_warning("Function run() has wrong number of arguments: %d.", nargs);
        goto fail;
    }
    if (!res)
        PyErr_Print();

fail:
    Py_XDECREF(res);
    Py_XDECREF(py_container);
    finalize_module_function(info);
}

static void
pygwy_graph_run(GwyGraph *graph, const gchar *name)
{
    PygwyModuleInfo *info;
    PyObject *py_graph = NULL, *res = NULL;
    gint nargs;

    if (!(info = prepare_to_run_module_function(name, "run")))
        return;
    py_graph = pygobject_new((GObject*)graph);
    nargs = find_out_number_of_arguments(info->func);
    if (nargs == 0) {
        /* Legacy API; module expects a global variable "graph" */
        PyDict_SetItemString(pygwy_dict, "graph", py_graph);
        res = PyObject_CallFunction(info->func, NULL);
        PyDict_DelItemString(pygwy_dict, "graph");
    }
    else if (nargs == 1)
        res = PyObject_CallFunction(info->func, "O", py_graph);
    else {
        g_warning("Function run() has wrong number of arguments: %d.", nargs);
        goto fail;
    }
    if (!res)
        PyErr_Print();

fail:
    Py_XDECREF(res);
    Py_XDECREF(py_graph);
    finalize_module_function(info);
}

static gboolean
pygwy_file_save_run(GwyContainer *data, const gchar *filename, GwyRunType mode,
                    GError **error,
                    const gchar *name)
{
    PyObject *py_container = NULL, *res = NULL;
    PygwyModuleInfo *info;
    gboolean ok = FALSE;
    gint nargs;

    if (!(info = prepare_to_run_module_function(name, "save"))) {
        err_PYTHON(error);
        return FALSE;
    }

    py_container = pygobject_new((GObject*)data);
    nargs = find_out_number_of_arguments(info->func);
    if (nargs == 1)
        res = PyObject_CallFunction(info->func, "Os", py_container, filename);
    else if (nargs == 2)
        res = PyObject_CallFunction(info->func, "Osi", py_container, filename, mode);
    else {
        g_warning("Function save() has wrong number of arguments: %d.", nargs);
        goto fail;
    }
    if (!res)
        PyErr_Print();
    if (res && PyInt_Check(res))
        ok = !!PyInt_AsLong(res);

fail:
    Py_XDECREF(res);
    Py_XDECREF(py_container);
    finalize_module_function(info);
    if (!ok)
        err_PYTHON(error);
    return ok;
}

static GwyContainer*
pygwy_file_load_run(const gchar *filename, GwyRunType mode, GError **error,
                    const gchar *name)
{
    GwyContainer *container = NULL;
    PyObject *res = NULL;
    PygwyModuleInfo *info;
    gchar *class_name;
    gint nargs;

    if (!(info = prepare_to_run_module_function(name, "load"))) {
        err_PYTHON(error);
        return NULL;
    }

    nargs = find_out_number_of_arguments(info->func);
    if (nargs == 1)
        res = PyObject_CallFunction(info->func, "s", filename);
    else if (nargs == 2)
        res = PyObject_CallFunction(info->func, "si", filename, mode);
    else {
        g_warning("Function load() has wrong number of arguments: %d.", nargs);
        goto fail;
    }
    if (!res) {
        PyErr_Print();
        goto fail;
    }

    class_name = find_out_class_name(res);
    if (class_name && gwy_strequal(class_name, "Container")) {
        PyGObject *pyg_res = (PyGObject*)res;
        container = (GwyContainer*)g_object_ref(pyg_res->obj);
    }
    g_free(class_name);

fail:
    Py_XDECREF(res);
    finalize_module_function(info);
    if (!container)
        err_PYTHON(error);
    gwy_debug("Return value %p", container);
    return container;
}

static gint
pygwy_file_detect_by_name_run(const GwyFileDetectInfo *fileinfo,
                              const gchar *name)
{
    PyObject *res = NULL;
    PygwyModuleInfo *info;
    gint score = 0;

    if (!(info = prepare_to_run_module_function(name, "detect_by_name")))
        return 0;

    res = PyObject_CallFunction(info->func, "s", fileinfo->name);
    if (!res)
        PyErr_Print();
    if (res && PyInt_Check(res))
        score = PyInt_AsLong(res);
    gwy_debug("Score for %s is %d (module %s)",
              fileinfo->name, score, info->name);

    Py_XDECREF(res);
    finalize_module_function(info);
    return score;
}

static gint
pygwy_file_detect_by_content_run(const GwyFileDetectInfo *fileinfo,
                                 const gchar *name)
{
    PyObject *res = NULL;
    PygwyModuleInfo *info;
    gint score = 0;

    if (!(info = prepare_to_run_module_function(name, "detect_by_content")))
        return 0;

    /* FIXME: This might not still be good enough for binary files. */
    res = PyObject_CallFunction(info->func, "ss#s#n",
                                fileinfo->name,
                                fileinfo->head,
                                (Py_ssize_t)fileinfo->buffer_len,
                                fileinfo->tail,
                                (Py_ssize_t)fileinfo->buffer_len,
                                (Py_ssize_t)fileinfo->file_size);
    if (!res)
        PyErr_Print();
    if (res && PyInt_Check(res))
        score = PyInt_AsLong(res);
    gwy_debug("Score for %s is %d (module %s)",
              fileinfo->name, score, info->name);

    Py_XDECREF(res);
    finalize_module_function(info);
    return score;
}

static gint
pygwy_file_detect_run(const GwyFileDetectInfo *fileinfo,
                      gboolean only_name,
                      const gchar *name)
{
    if (only_name)
        return pygwy_file_detect_by_name_run(fileinfo, name);
    return pygwy_file_detect_by_content_run(fileinfo, name);
}

static PygwyModuleInfo*
pygwy_find_module(const gchar* name)
{
    GList *l = registered_modules;
    PygwyModuleInfo *info;

    for (l = registered_modules; l; l = l->next) {
        info = (PygwyModuleInfo*)(l->data);
        if (gwy_strequal(info->name, name))
            return info;
    }
    g_warning("Cannot find record for Python module '%s'", name);
    return NULL;
}

static PyObject*
pygwy_check_func(PyObject *module, const gchar *name, gchar *filename)
{
    PyObject *func;

    if (!module) {
        g_warning("Undefined pygwy module == NULL ('%s')", filename);
        return NULL;
    }
    func = PyDict_GetItemString(PyModule_GetDict(module), name);
    if (!func) {
        g_warning("Function '%s' not found in '%s'", name, filename);
        return NULL;
    }
    if (!PyCallable_Check(func)) {
        g_warning("Function '%s' in '%s' is not callable.", name, filename);
        return NULL;
    }
    return func;
}

/* Conract: ensure info has valid code, dict, module and func, or we return
 * NULL and dict, module and func are unset in the info (if it exists). */
static PygwyModuleInfo*
prepare_to_run_module_function(const gchar *name, const gchar *funcname)
{
    PygwyModuleInfo *info;

    if (!(info = pygwy_find_module(name))) {
        g_warning("Cannot find module '%s'.", name);
        return NULL;
    }
    gwy_debug("Preparing to run function `%s' in module '%s', filename '%s'",
              funcname, info->name, info->fullpath);

    if (!(info->dict = pygwy_create_environment(info->fullpath, TRUE)))
        return NULL;

    if (!update_module_code(info)) {
        finalize_module_function(info);
        return NULL;
    }

    if (!(info->module = PyImport_ExecCodeModule(info->name, info->code))) {
        PyErr_Print();
        finalize_module_function(info);
        return NULL;
    }

    if (!(info->func = pygwy_check_func(info->module, funcname,
                                        info->fullpath))) {
        finalize_module_function(info);
        return NULL;
    }
    gwy_debug("Running function `%s' in module '%s', filename '%s'",
              funcname, info->name, info->fullpath);

    return info;
}

static void
finalize_module_function(PygwyModuleInfo *info)
{
    Py_XDECREF(info->module);
    destroy_environment(info->dict, TRUE);
    info->module = info->func = info->dict = NULL;
}

/* Obtain obj.attr1.attr2, etc. */
static PyObject*
get_attribute_recursive(PyObject *obj, ...)
{
    PyObject *attrobj;
    GSList *l, *to_free = NULL;
    va_list ap;
    const gchar *attrname;

    if (!obj)
        return NULL;

    va_start(ap, obj);
    while ((attrname = va_arg(ap, const gchar*))) {
        attrobj = PyObject_GetAttrString(obj, attrname);
        /* If we fail, unref the entire list in reverse order. */
        if (!attrobj) {
            va_end(ap);
            for (l = to_free; l; l = l->next)
                Py_XDECREF((PyObject*)l->data);
            g_slist_free(to_free);
            return NULL;
        }
        to_free = g_slist_prepend(to_free, attrobj);
        obj = attrobj;
    }
    va_end(ap);
    /* We succeeded; unref everything execpt the object we want to return. */
    for (l = to_free; l; l = l->next) {
        if ((PyObject*)l->data != obj)
            Py_XDECREF((PyObject*)l->data);
    }
    g_slist_free(to_free);
    return obj;
}

/* This does not work with anonymous arguments but anonymous arguments are
 * module API violation so who cares. */
static gint
find_out_number_of_arguments(PyObject *func)
{
    PyObject *nargs = get_attribute_recursive(func,
                                              "func_code", "co_argcount", NULL);
    gint n = -1;

    if (nargs && PyInt_Check(nargs))
        n = PyInt_AsLong(nargs);
    Py_XDECREF(nargs);
    return n;
}

static gchar*
find_out_class_name(PyObject *obj)
{
    PyObject *name = get_attribute_recursive(obj,
                                             "__class__", "__name__", NULL);
    gchar *s = NULL;

    if (name && PyString_Check(name))
        s = g_strdup(PyString_AsString(name));
    Py_XDECREF(name);
    return s;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
