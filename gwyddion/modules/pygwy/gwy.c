/*
 *  $Id: gwy.c 21527 2018-10-28 09:02:14Z yeti-dn $
 *  Copyright (C) 2012-2016 David Necas (Yeti), Petr Klapetek, Jozef Vesely.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, vesely@gjh.sk.
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

#include "config.h"
#include <pygobject.h>
#include <app/gwyapp.h>
#include "wrap_calls.h"
#include "pygwy.h"

#ifdef G_OS_WIN32
#include <windows.h>
#include <winreg.h>
#define gwyddion_key "Software\\Gwyddion\\2.0"
#endif

#include "pygwywrap.c"
#include "pygwy-console.h"

static void
load_modules(void)
{
    const gchar *const module_types[] = {
        "file", "layer", "process", "graph", "tools", NULL
    };
    GPtrArray *module_dirs;
    const gchar *upath;
    gchar *mpath;
    guint i;

    module_dirs = g_ptr_array_new();

    /* System modules */
    mpath = gwy_find_self_dir("modules");
    for (i = 0; module_types[i]; i++) {
        g_ptr_array_add(module_dirs,
                        g_build_filename(mpath, module_types[i], NULL));
    }
    g_free(mpath);

    /* User modules */
    upath = gwy_get_user_dir();
    for (i = 0; module_types[i]; i++) {
        g_ptr_array_add(module_dirs,
                        g_build_filename(upath, module_types[i], NULL));
    }

    /* Register all found there, in given order. */
    g_ptr_array_add(module_dirs, NULL);
    gwy_module_register_modules((const gchar**)module_dirs->pdata);

    for (i = 0; module_dirs->pdata[i]; i++)
        g_free(module_dirs->pdata[i]);
    g_ptr_array_free(module_dirs, TRUE);
}

/* FIXME: It would be better to just fix the flags using RTLD_NOLOAD because
 * the libraries are surely loaded.  But that is not possible using the
 * GModule API.  */
static gboolean
reload_libraries(void)
{
    /* Do not do it on Win32 because the libs must be fully resolved there and
     * because the library names are different (with -0). */
#ifndef G_OS_WIN32
    static const gchar *const gwyddion_libs[] = {
        "libgwyddion2", "libgwyprocess2", "libgwydraw2", "libgwydgets2",
        "libgwymodule2", "libgwyapp2",
    };
#ifdef __APPLE__
    /* Darwin has soname_spec='$libname$release$major$shared_ext' */
    static const gchar *soname_format = "%s.0.%s";
#else
    /* ELF systems generally have
     * soname_spec='$libname$release$shared_ext$major' */
    static const gchar *soname_format = "%s.%s.0";
#endif
    guint i;

    for (i = 0; i < G_N_ELEMENTS(gwyddion_libs); i++) {
        gchar *filename = g_strdup_printf(soname_format,
                                          gwyddion_libs[i],
                                          GWY_SHARED_LIBRARY_EXTENSION);
        GModule *modhandle = g_module_open(filename, G_MODULE_BIND_LAZY);
        if (!modhandle) {
            gchar *excstr = g_strdup_printf("Cannot dlopen() %s.", filename);
            PyErr_SetString(PyExc_ImportError, excstr);
            g_free(excstr);
            return FALSE;
        }
        g_module_make_resident(modhandle);
        g_free(filename);
    }
#endif

    return TRUE;
}

/* Special libraries modules are linked with are not found, for whatever
 * reason.  It does not help that this module is linked with them either.
 * They are not unresolved, they are just not found.  So we temporarily
 * switch to the Gwyddion install path. */
static void
switch_between_gwyddion_bin_dir(G_GNUC_UNUSED gboolean back)
{
#ifdef G_OS_WIN32
    static wchar_t orig_cwd[PATH_MAX];

    gchar installdir[PATH_MAX];
    DWORD size = sizeof(installdir)-1;
    HKEY reg_key;

    if (back) {
        _wchdir(orig_cwd);
        return;
    }

    _wgetcwd(orig_cwd, PATH_MAX);
    orig_cwd[PATH_MAX-1] = '\0';

    gwy_clear(installdir, sizeof(installdir));
    if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT(gwyddion_key),
                     0, KEY_READ, &reg_key) == ERROR_SUCCESS) {
        if (RegQueryValueEx(reg_key, TEXT("InstallDir"), NULL, NULL,
                            installdir, &size) == ERROR_SUCCESS) {
            RegCloseKey(reg_key);
            if (size + 5 <= PATH_MAX) {
                memcpy(installdir + size-1, "\\bin", 5);
                chdir(installdir);
            }
            return;
        }
        RegCloseKey(reg_key);
    }
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT(gwyddion_key),
                     0, KEY_READ, &reg_key) == ERROR_SUCCESS) {
        if (RegQueryValueEx(reg_key, TEXT("InstallDir"), NULL, NULL,
                            installdir, &size) == ERROR_SUCCESS) {
            if (size + 5 <= PATH_MAX) {
                memcpy(installdir + size-1, "\\bin", 5);
                chdir(installdir);
            }
        }
        RegCloseKey(reg_key);
    }
#endif
}

PyMODINIT_FUNC
initgwy(void)
{
    gchar *settings_file;
    PyObject *mod, *dict;

    switch_between_gwyddion_bin_dir(FALSE);

    if (!reload_libraries())
        return;

    /* This requires a display.  */
    gtk_init(NULL, NULL);
    gwy_widgets_type_init();
    gwy_undo_set_enabled(FALSE);
    gwy_app_wait_set_enabled(FALSE);
    gwy_app_data_browser_set_gui_enabled(FALSE);
    gwy_resource_class_load(g_type_class_peek(GWY_TYPE_GRADIENT));
    gwy_resource_class_load(g_type_class_peek(GWY_TYPE_GL_MATERIAL));
    gwy_resource_class_load(g_type_class_peek(GWY_TYPE_GRAIN_VALUE));
    gwy_resource_class_load(g_type_class_peek(GWY_TYPE_CALIBRATION));
    settings_file = gwy_app_settings_get_settings_filename();
    gwy_app_settings_load(settings_file, NULL);
    g_free(settings_file);
    /* This requires a display.  */
    gwy_stock_register_stock_items();
    load_modules();

    /* pygwy.c */
    init_pygobject();
    mod = Py_InitModule("gwy", (PyMethodDef*)pygwy_functions);
    dict = PyModule_GetDict(mod);
    /* This does "import gtk" so display is required. */
    pygwy_register_classes(dict);
    pygwy_add_constants(mod, "GWY_");

    switch_between_gwyddion_bin_dir(TRUE);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
