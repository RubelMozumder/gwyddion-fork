/*
 *  $Id: gwymoduleloader.c 23946 2021-08-10 11:16:22Z yeti-dn $
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
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>

#include "gwymoduleinternal.h"

#ifdef G_OS_WIN32
#include <windows.h>
#include <winreg.h>
#define python_version "2.7"
#define python_key "Software\\Python\\PythonCore\\" python_version "\\InstallPath"
#endif

#undef GWY_MODULE_PEDANTIC_CHECK

typedef struct {
    GHFunc func;
    gpointer data;
} GwyModuleForeachData;

typedef struct {
    GFunc func;
    gpointer data;
} GwyModuleFailForeachData;

static gboolean             check_python_availability     (void);
static void                 gwy_load_modules_in_dir       (GDir *gdir,
                                                           const gchar *dirname,
                                                           GHashTable *mods);
static gboolean             gwy_module_pedantic_check     (_GwyModuleInfoInternal *iinfo);
static void                 gwy_module_get_rid_of         (const gchar *modname);
static void                 gwy_module_init               (void);
static const GwyModuleInfo* gwy_module_do_register_module (const gchar *modulename,
                                                           GHashTable *mods,
                                                           GError **error);
static gboolean             gwy_module_check_module_name  (const gchar *modname,
                                                           GHashTable *mods,
                                                           GError **error);
static gboolean             register_module_with_info     (GHashTable *mods,
                                                           const GwyModuleInfo *mod_info,
                                                           const gchar *filename,
                                                           gchar *modname,
                                                           gboolean in_bundle,
                                                           GError **error);
static gchar*               gwy_module_figure_out_name    (const gchar *filename);
static void                 gwy_module_register_fail      (GError *myerr,
                                                           GError **error,
                                                           const gchar *modname,
                                                           const gchar *filename);
static GHashTable*          gwy_module_get_blocking_table (gboolean do_create);
static gboolean             gwy_module_filename_is_blocked(const gchar *filename);
static gboolean             gwy_module_name_is_blocked    (const gchar *modname);

static GHashTable *modules = NULL;
static GHashTable *failures = NULL;
static gboolean modules_initialized = FALSE;
static gchar *currenly_registered_module = NULL;

/**
 * gwy_module_error_quark:
 *
 * Returns error domain for module loading.
 *
 * See and use %GWY_MODULE_ERROR.
 *
 * Returns: The error domain.
 **/
GQuark
gwy_module_error_quark(void)
{
    static GQuark error_domain = 0;

    if (!error_domain)
        error_domain = g_quark_from_static_string("gwy-module-error-quark");

    return error_domain;
}

/**
 * gwy_module_register_modules:
 * @paths: A %NULL-terminated list of directory names.
 *
 * Registers all modules in given directories.
 *
 * It can be called several times (on different directories).  No errors are reported, register modules individually
 * with gwy_module_register_module() to get registration errors.
 *
 * If you need to prevent specific modules from loading use gwy_module_disable_registration() beforehand.
 **/
void
gwy_module_register_modules(const gchar **paths)
{
    const gchar *dir;

    if (!modules_initialized)
        gwy_module_init();
    if (!paths)
        return;

    for (dir = *paths; dir; dir = *(++paths)) {
        GError *err = NULL;
        GDir *gdir;

        gwy_debug("Opening module directory %s", dir);
        gdir = g_dir_open(dir, 0, &err);
        if (err) {
            gwy_debug("Cannot open module directory %s: %s", dir, err->message);
            g_clear_error(&err);
            continue;
        }

        gwy_load_modules_in_dir(gdir, dir, modules);
        g_dir_close(gdir);
    }
}

gboolean
_gwy_module_add_registered_function(const gchar *prefix,
                                    const gchar *name)
{
    _GwyModuleInfoInternal *info;

    g_return_val_if_fail(modules_initialized, FALSE);
    g_return_val_if_fail(currenly_registered_module, FALSE);
    info = g_hash_table_lookup(modules, currenly_registered_module);
    g_return_val_if_fail(info, FALSE);

    info->funcs = g_slist_append(info->funcs, g_strconcat(prefix, name, NULL));
    return TRUE;
}

static void
gwy_module_failure_foreach_one(G_GNUC_UNUSED const gchar *key,
                               _GwyModuleFailureInfoInternal *finfo,
                               GwyModuleFailForeachData *data)
{
    data->func((GwyModuleFailureInfo*)finfo, data->data);
}

/**
 * gwy_module_failure_foreach:
 * @function: A #GFunc run for each module registration failure.
 * @data: User data.
 *
 * Runs @function for each module that failed to register.
 *
 * It passes the failure info (#GwyModuleFailureInfo) as the data argument. It should be modified.
 *
 * Since: 2.49
 **/
void
gwy_module_failure_foreach(GFunc function, gpointer data)
{
    GwyModuleFailForeachData fdata;

    if (!failures)
        return;

    g_return_if_fail(function);
    fdata.func = function;
    fdata.data = data;
    g_hash_table_foreach(failures, (GHFunc)&gwy_module_failure_foreach_one, &fdata);
}

static void
gwy_module_foreach_one(const gchar *name,
                       _GwyModuleInfoInternal *iinfo,
                       GwyModuleForeachData *fdata)
{
    fdata->func((gpointer)name, (gpointer)iinfo->mod_info, fdata->data);
}

/**
 * gwy_module_foreach:
 * @function: A #GHFunc run for each module.
 * @data: User data.
 *
 * Runs @function on each registered module.
 *
 * It passes module name as the key and pointer to module info (#GwyModuleInfo) as the value.  Neither should be
 * modified.
 **/
void
gwy_module_foreach(GHFunc function,
                   gpointer data)
{
    GwyModuleForeachData fdata;

    g_return_if_fail(modules_initialized);

    fdata.data = data;
    fdata.func = function;
    g_hash_table_foreach(modules, (GHFunc)gwy_module_foreach_one, &fdata);
}

/**
 * gwy_module_get_filename:
 * @name: A module name.
 *
 * Returns full file name of a module.
 *
 * Returns: Module file name as a string that must be modified or freed.
 **/
const gchar*
gwy_module_get_filename(const gchar *name)
{
    _GwyModuleInfoInternal *iinfo;

    g_return_val_if_fail(modules_initialized, NULL);

    iinfo = g_hash_table_lookup(modules, name);
    if (!iinfo) {
        g_warning("No such module loaded");
        return NULL;
    }

    return iinfo->file;
}

/**
 * gwy_module_get_functions:
 * @name: A module name.
 *
 * Returns list of names of functions a module implements.
 *
 * Returns: List of module function names, as a #GSList that is owned by module loader and must not be modified or
 *          freed.
 **/
GSList*
gwy_module_get_functions(const gchar *name)
{
    _GwyModuleInfoInternal *iinfo;

    g_return_val_if_fail(modules_initialized, NULL);

    iinfo = g_hash_table_lookup(modules, name);
    if (!iinfo) {
        g_warning("No such module loaded");
        return NULL;
    }

    return iinfo->funcs;
}

/**
 * gwy_module_register_module:
 * @name: Module file name to load, including full path and extension.
 * @error: Location to store error, or %NULL to ignore them.  Errors from #GwyModuleError domain can occur.
 *
 * Loads a single module.
 *
 * This function also works with bundles.  The returned module info is for the bundle and thus not of much use.
 *
 * Returns: Module info on success, %NULL on failure.
 **/
const GwyModuleInfo*
gwy_module_register_module(const gchar *name,
                           GError **error)
{
    if (!modules_initialized)
        gwy_module_init();

    return gwy_module_do_register_module(name, modules, error);
}

static void
gwy_module_register_fail(GError *myerr,
                         GError **error,
                         const gchar *modname,
                         const gchar *filename)
{
    _GwyModuleFailureInfoInternal *finfo;

    if (!failures)
        failures = g_hash_table_new(g_str_hash, g_str_equal);

    if (g_hash_table_lookup(failures, filename)) {
        g_clear_error(&myerr);
        return;
    }

    g_return_if_fail(myerr);
    finfo = g_new(_GwyModuleFailureInfoInternal, 1);
    finfo->key = g_strconcat(filename, "/", modname, NULL);
    finfo->modname = g_strdup(modname);
    finfo->filename = g_strdup(filename);
    finfo->err_message = g_strdup(myerr->message);
    finfo->err_domain = myerr->domain;
    finfo->err_code = myerr->code;
    g_hash_table_insert(failures, finfo->key, finfo);

    g_propagate_error(error, myerr);
}

static const GwyModuleInfo*
gwy_module_do_register_module(const gchar *filename,
                              GHashTable *mods,
                              GError **error)
{
    GModule *mod;
    gboolean ok;
    GwyModuleInfo *mod_info = NULL;
    GwyModuleQueryFunc query;
    gchar *modname;
    GError *err = NULL;

    modname = gwy_module_figure_out_name(filename);
    if (!gwy_module_check_module_name(modname, mods, &err)) {
        gwy_module_register_fail(err, error, modname, filename);
        g_free(modname);
        return NULL;
    }

    gwy_debug("Trying to load module `%s' from file `%s'.", modname, filename);
    mod = g_module_open(filename, G_MODULE_BIND_LAZY);

    if (!mod) {
        g_set_error(&err, GWY_MODULE_ERROR, GWY_MODULE_ERROR_OPEN, "Cannot open module: %s", g_module_error());
        gwy_module_register_fail(err, error, modname, filename);
        g_free(modname);
        return NULL;
    }
    gwy_debug("Module loaded successfully as `%s'.", g_module_name(mod));

    /* Sanity checks on the module before registration is attempted. */
    ok = TRUE;
    currenly_registered_module = modname;
    if (!g_module_symbol(mod, "_gwy_module_query", (gpointer)&query) || !query) {
        g_set_error(&err, GWY_MODULE_ERROR, GWY_MODULE_ERROR_QUERY, "Module contains no query function");
        gwy_module_register_fail(err, error, modname, filename);
        ok = FALSE;
    }

    if (ok) {
        mod_info = query();
        /* register_module_with_info() always consumes (or frees) modname */
        ok = register_module_with_info(mods, mod_info, filename, modname, FALSE, error);
        if (ok) {
            gwy_debug("Making module `%s' resident.", filename);
            g_module_make_resident(mod);
        }
        else {
            if (!g_module_close(mod)) {
                g_critical("Cannot unload module `%s': %s", filename, g_module_error());
            }
        }
    }
    else
        g_free(modname);

    currenly_registered_module = NULL;

    return ok ? mod_info : NULL;
}

static gboolean
register_module_bundle(GHashTable *mods, const GwyModuleInfo *bundle_info,
                       const gchar *filename, const gchar *bundlename,
                       GError **error)
{
    const GwyModuleInfo *mod_info;
    GwyModuleQueryFunc query;
    const GwyModuleRecord *records;
    GwyModuleBundleRegisterFunc register_bundle;
    GError *err = NULL;
    gchar *modname;
    guint i, nok = 0;

    if (!bundle_info->register_func) {
        g_set_error(&err, GWY_MODULE_ERROR, GWY_MODULE_ERROR_ABI,
                    "Module bundle %s info has no registration function", bundlename);
        gwy_module_register_fail(err, error, bundlename, filename);
        return FALSE;
    }

    register_bundle = (GwyModuleBundleRegisterFunc)(bundle_info->register_func);
    records = register_bundle();
    if (!records) {
        g_set_error(&err, GWY_MODULE_ERROR, GWY_MODULE_ERROR_ABI,
                    "Module bundle %s returned NULL module records", bundlename);
        gwy_module_register_fail(err, error, bundlename, filename);
        return FALSE;
    }

    for (i = 0; records[i].query && records[i].name; i++) {
        gwy_debug("bundle module record for %s", records[i].name);
        if (gwy_module_name_is_blocked(records[i].name))
            continue;

        err = NULL;
        if (!gwy_module_check_module_name(records[i].name, mods, &err)) {
            gwy_module_register_fail(err, error, records[i].name, filename);
            continue;
        }

        query = records[i].query;
        modname = g_strdup(records[i].name);
        currenly_registered_module = modname;
        mod_info = query();
        if (mod_info) {
            /* TODO: Must again check duplicit names, etc. */
            if (register_module_with_info(mods, mod_info, filename, modname, TRUE, NULL))
                nok++;
        }
        else
            g_free(modname);

        currenly_registered_module = NULL;
    }

    if (nok > 0)
        return TRUE;

    /* FIXME: Could be report errors in more detail? */
    g_set_error(error, GWY_MODULE_ERROR, GWY_MODULE_ERROR_ABI,
                "Module bundle %s did not successfully register any module", bundlename);
    return FALSE;
}

static gboolean
register_module_with_info(GHashTable *mods, const GwyModuleInfo *mod_info,
                          const gchar *filename, gchar *modname,
                          gboolean in_bundle, GError **error)
{
    _GwyModuleInfoInternal *iinfo;
    GError *err = NULL;
    gboolean ok = TRUE;
    guint abi_version;

    if (!mod_info) {
        g_set_error(&err, GWY_MODULE_ERROR, GWY_MODULE_ERROR_INFO, "Module info is NULL");
        gwy_module_register_fail(err, error, modname, filename);
        ok = FALSE;
    }

    if (ok) {
        abi_version = mod_info->abi_version;
        if (abi_version & GWY_MODULE_BUNDLE_FLAG) {
            gwy_debug("bundle flag found on %s", filename);
            if (in_bundle) {
                g_set_error(&err, GWY_MODULE_ERROR, GWY_MODULE_ERROR_NESTING,
                            "Nested module bundles are insane and not supported.");
                gwy_module_register_fail(err, error, modname, filename);
                g_free(modname);
                return FALSE;
            }
            abi_version &= ~(GWY_MODULE_BUNDLE_FLAG);
            if (abi_version == GWY_MODULE_ABI_VERSION) {
                ok = register_module_bundle(mods, mod_info, filename, modname, error);
                g_free(modname);
                return ok;
            }
            ok = FALSE;
        }
        else {
            ok = (abi_version == GWY_MODULE_ABI_VERSION);
        }

        if (!ok) {
            g_set_error(&err, GWY_MODULE_ERROR, GWY_MODULE_ERROR_ABI,
                        "Module ABI version %d differs from %d", mod_info->abi_version, GWY_MODULE_ABI_VERSION);
            gwy_module_register_fail(err, error, modname, filename);
        }
    }

    if (ok) {
        ok = (mod_info->register_func
              && mod_info->blurb && *mod_info->blurb
              && mod_info->author && *mod_info->author
              && mod_info->version && *mod_info->version
              && mod_info->copyright && *mod_info->copyright
              && mod_info->date && *mod_info->date);
        if (!ok) {
            g_set_error(&err, GWY_MODULE_ERROR, GWY_MODULE_ERROR_ABI, "Module info has missing/invalid fields");
            gwy_module_register_fail(err, error, modname, filename);
        }
    }

    if (ok) {
        iinfo = g_new(_GwyModuleInfoInternal, 1);
        iinfo->mod_info = mod_info;
        iinfo->name = modname;
        iinfo->file = g_strdup(filename);
        iinfo->loaded = TRUE;
        iinfo->funcs = NULL;
        g_hash_table_insert(mods, (gpointer)iinfo->name, iinfo);
        if (!(ok = mod_info->register_func())) {
            g_set_error(&err, GWY_MODULE_ERROR, GWY_MODULE_ERROR_REGISTER, "Module feature registration failed");
            gwy_module_register_fail(err, error, modname, filename);
        }
        if (ok && !iinfo->funcs) {
            g_set_error(&err, GWY_MODULE_ERROR, GWY_MODULE_ERROR_REGISTER, "Module did not register any function");
            gwy_module_register_fail(err, error, modname, filename);
            ok = FALSE;
        }

        if (ok)
            gwy_module_pedantic_check(iinfo);
        else
            gwy_module_get_rid_of(iinfo->name);
    }
    else
        g_free(modname);

    return ok;
}

static gchar*
gwy_module_figure_out_name(const gchar *filename)
{
    gchar *modname, *s;
    guint i;

    modname = g_path_get_basename(filename);
    for (i = 0; modname[i]; i++)
        modname[i] = g_ascii_tolower(modname[i]);

    /* FIXME: On normal platforms module names have an extension, but if it doesn't, just get over it.  This can
     * happen only with explicit gwy_module_register_module() as gwy_load_modules_in_dir() accepts only sane names. */
    if ((s = strchr(modname, '.')))
        *s = '\0';

    return modname;
}

static gboolean
gwy_module_check_module_name(const gchar *modname,
                             GHashTable *mods,
                             GError **error)
{
    if (!modname || !*modname) {
        g_set_error(error, GWY_MODULE_ERROR, GWY_MODULE_ERROR_NAME, "Module name is empty");
        return FALSE;
    }

    if (!gwy_strisident(modname, "_-", NULL))
        g_warning("Module name `%s' is not a valid identifier. It may be rejected in future.", modname);

    if (g_hash_table_lookup(mods, modname)) {
        g_set_error(error, GWY_MODULE_ERROR, GWY_MODULE_ERROR_DUPLICATE, "Module was already registered");
        return FALSE;
    }

    if (gwy_strequal(modname, "pygwy") && !check_python_availability()) {
        g_set_error(error, GWY_MODULE_ERROR, GWY_MODULE_ERROR_OPEN,
                    "Avoiding to register pygwy if Python is unavailable.");
        return FALSE;
    }

    return TRUE;
}

/* XXX: If python is not available loading pygwy can pop up weird boxes.  Fix
 * it here. */
static gboolean
check_python_availability(void)
{
#ifdef G_OS_WIN32
    gchar pythondir[256];
    DWORD size = sizeof(pythondir)-1;
    HKEY reg_key;
    gboolean ok = FALSE;

    gwy_clear(pythondir, sizeof(pythondir));
    if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT(python_key), 0, KEY_READ, &reg_key) == ERROR_SUCCESS) {
        if (RegQueryValueEx(reg_key, NULL, NULL, NULL, pythondir, &size) == ERROR_SUCCESS)
            ok = TRUE;
        RegCloseKey(reg_key);
    }
    if (!ok && RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT(python_key), 0, KEY_READ, &reg_key) == ERROR_SUCCESS) {
        if (RegQueryValueEx(reg_key, NULL, NULL, NULL, pythondir, &size) == ERROR_SUCCESS)
            ok = TRUE;
        RegCloseKey(reg_key);
    }

    if (!ok) {
        g_message("Cannot get %s registry key, assuming no python 2.7.", python_key);
        return FALSE;
    }
#endif

    return TRUE;
}

#ifdef G_OS_WIN32
static gboolean
gwy_str_has_suffix_nocase(const gchar *s,
                          const gchar *suffix)
{
    guint len, suffix_len;

    if (!suffix || !*suffix)
        return TRUE;

    g_return_val_if_fail(s, FALSE);

    len = strlen(s);
    suffix_len = strlen(suffix);
    if (len < suffix_len)
        return FALSE;

    return !g_ascii_strcasecmp(s + len - suffix_len, suffix);
}
#endif

static void
gwy_load_modules_in_dir(GDir *gdir,
                        const gchar *dirname,
                        GHashTable *mods)
{
    const gchar *filename;
    gchar *modulename;

    modulename = NULL;
    while ((filename = g_dir_read_name(gdir))) {
        if (g_str_has_prefix(filename, "."))
            continue;
#ifdef G_OS_WIN32
        if (!gwy_str_has_suffix_nocase(filename, "." G_MODULE_SUFFIX))
#else
        if (!g_str_has_suffix(filename, "." G_MODULE_SUFFIX))
#endif
            continue;

        /* FIXME: Should we block bundles here?  Probably, because we may want
         * to block things *before* we try to dlopen() them.  This allows to
         * get out of some hairy situations. */
        if (!gwy_module_filename_is_blocked(filename)) {
            modulename = g_build_filename(dirname, filename, NULL);
            gwy_module_do_register_module(modulename, mods, NULL);
            g_free(modulename);
        }
    }
}

#ifdef GWY_MODULE_PEDANTIC_CHECK
static gboolean
gwy_module_pedantic_check(_GwyModuleInfoInternal *iinfo)
{
    const gchar *p;
    gboolean ok = TRUE;
    GSList *l;

    if (g_str_has_prefix(iinfo->funcs->data, GWY_MODULE_PREFIX_LAYER)) {
        for (l = iinfo->funcs; l; l = g_slist_next(l)) {
            p = strchr((const gchar*)l->data, ':');
            g_return_val_if_fail(p && p[1] == ':', FALSE);
            p += 2;

            if (!g_str_has_prefix(p, "GwyLayer")) {
                g_warning("Module `%s' registered layer function `%s' whose name has not the form `GwyLayerFoo'.",
                          iinfo->name, p);
                ok = FALSE;
            }
        }
        return ok;
    }

    if (g_str_has_prefix(iinfo->funcs->data, GWY_MODULE_PREFIX_TOOL)) {
        for (l = iinfo->funcs; l; l = g_slist_next(l)) {
            p = strchr((const gchar*)l->data, ':');
            g_return_val_if_fail(p && p[1] == ':', FALSE);
            p += 2;

            if (!g_str_has_prefix(p, "GwyTool")) {
                g_warning("Module `%s' registered tool function `%s' whose name has not the form `GwyToolFoo'.",
                          iinfo->name, p);
                ok = FALSE;
            }
        }
        return ok;
    }

    if (g_slist_length(iinfo->funcs) == 1) {
        p = strchr((const gchar*)iinfo->funcs->data, ':');
        g_return_val_if_fail(p && p[1] == ':', FALSE);
        p += 2;

        if (!gwy_strequal(iinfo->name, p)) {
            g_warning("Module `%s' registered only one function `%s' and its name differs from module name.  Usually, "
                      "these two names should be the same.",
                      iinfo->name, p);
            return FALSE;
        }
    }

    return TRUE;
}
#else
static gboolean
gwy_module_pedantic_check(G_GNUC_UNUSED _GwyModuleInfoInternal *iinfo)
{
    return TRUE;
}
#endif

static void
gwy_module_get_rid_of(const gchar *modname)
{
    static const struct {
        const gchar *prefix;
        gboolean (*func)(const gchar*);
    }
    gro_funcs[] = {
        { GWY_MODULE_PREFIX_PROC,   _gwy_process_func_remove, },
        { GWY_MODULE_PREFIX_FILE,   _gwy_file_func_remove,    },
        { GWY_MODULE_PREFIX_GRAPH,  _gwy_graph_func_remove,   },
        { GWY_MODULE_PREFIX_TOOL,   _gwy_tool_func_remove,    },
        { GWY_MODULE_PREFIX_LAYER,  _gwy_layer_func_remove,   },
        { GWY_MODULE_PREFIX_VOLUME, _gwy_volume_func_remove,  },
        { GWY_MODULE_PREFIX_XYZ,    _gwy_xyz_func_remove,     },
        { GWY_MODULE_PREFIX_CMAP,   _gwy_cmap_func_remove,    },
    };

    _GwyModuleInfoInternal *iinfo;
    GSList *l;
    gsize i;

    gwy_debug("%s", modname);
    iinfo = g_hash_table_lookup(modules, modname);
    g_return_if_fail(iinfo);
    /* FIXME: this is quite crude, it can remove functions of the same name
     * in different module type */
    for (l = iinfo->funcs; l; l = g_slist_next(l)) {
        gchar *canon_name = (gchar*)iinfo->funcs->data;

        for (i = 0; i < G_N_ELEMENTS(gro_funcs); i++) {
            if (g_str_has_prefix(canon_name, gro_funcs[i].prefix)
                && gro_funcs[i].func(canon_name + strlen(gro_funcs[i].prefix)))
                break;
        }
        if (i == G_N_ELEMENTS(gro_funcs)) {
            g_critical("Unable to find out %s function type", canon_name);
        }
        g_free(canon_name);
    }
    g_slist_free(iinfo->funcs);
    iinfo->funcs = NULL;
    g_hash_table_remove(modules, (gpointer)iinfo->name);
    g_free(iinfo->name);
    g_free(iinfo->file);
    g_free(iinfo);
}

/**
 * gwy_module_init:
 *
 * Initializes the loadable module system, aborting if there's no support for modules on the platform.
 *
 * Must be called at most once.  It is automatically called on first gwy_module_register_modules() call.
 **/
static void
gwy_module_init(void)
{
    g_return_if_fail(!modules_initialized);

    /* Check whether modules are supported. */
    if (!g_module_supported()) {
        g_error("Cannot initialize modules: not supported on this platform.");
    }

    modules = g_hash_table_new(g_str_hash, g_str_equal);
    modules_initialized = TRUE;
}

/**
 * gwy_module_lookup:
 * @name: A module name.
 *
 * Returns information about one module.
 *
 * Returns: The module info, of %NULL if not found.  It must be considered constant and never modified or freed.
 **/
const GwyModuleInfo*
gwy_module_lookup(const gchar *name)
{
    _GwyModuleInfoInternal *info;

    if (!modules)
        return NULL;

    info = g_hash_table_lookup(modules, name);
    return info ? info->mod_info : NULL;
}

/**
 * gwy_module_disable_registration:
 * @name: A module name.
 *
 * Prevents the registration of a module of given name.
 *
 * This function blocks future module registration using gwy_module_register_modules().  Already loaded modules are
 * unaffected. The low-level module loading function gwy_module_register_module() always attempts to load the module,
 * even if blocked.
 *
 * Since: 2.48
 **/
void
gwy_module_disable_registration(const gchar *name)
{
    GHashTable *blocked_modules;
    GQuark quark;

    g_return_if_fail(name);
    blocked_modules = gwy_module_get_blocking_table(TRUE);
    quark = g_quark_from_string(name);
    g_hash_table_insert(blocked_modules, GSIZE_TO_POINTER(quark), GINT_TO_POINTER(TRUE));
}

/**
 * gwy_module_enable_registration:
 * @name: A module name.
 *
 * Unblocks the registration of a module of given name.
 *
 * This function influences future module registration.  Already loaded modules are unaffected.
 *
 * Since: 2.48
 **/
void
gwy_module_enable_registration(const gchar *name)
{
    GHashTable *blocked_modules;
    GQuark quark;

    g_return_if_fail(name);
    blocked_modules = gwy_module_get_blocking_table(FALSE);
    if (!blocked_modules)
        return;

    quark = g_quark_from_string(name);
    g_hash_table_remove(blocked_modules, GSIZE_TO_POINTER(quark));
}

/**
 * gwy_module_is_enabled:
 * @name: A module name.
 *
 * Reports whether the registration of a module is enabled.
 *
 * If the registration of module @name was prevented using gwy_module_disable_registration() and not subsequently
 * re-enabled using gwy_module_enabled_registration() this function returns %FALSE.
 *
 * The reported values only represents the current state of blocking.  A module @name could have been loaded when it
 * was not blocked.
 *
 * Returns: %TRUE if module @name can be registered; %FALSE when it is blocked from registration.
 *
 * Since: 2.48
 **/
gboolean
gwy_module_is_enabled(const gchar *name)
{
    GHashTable *blocked_modules;
    GQuark quark;

    g_return_val_if_fail(name, FALSE);
    blocked_modules = gwy_module_get_blocking_table(FALSE);
    if (!blocked_modules)
        return TRUE;

    quark = g_quark_from_string(name);
    return !g_hash_table_lookup(blocked_modules, GSIZE_TO_POINTER(quark));
}

static gboolean
gwy_module_filename_is_blocked(const gchar *filename)
{
    GHashTable *blocked_modules;
    gchar *modname;
    GQuark quark;

    blocked_modules = gwy_module_get_blocking_table(FALSE);
    if (!blocked_modules)
        return FALSE;

    modname = gwy_module_figure_out_name(filename);
    quark = g_quark_from_string(modname);
    g_free(modname);

    return !!g_hash_table_lookup(blocked_modules, GSIZE_TO_POINTER(quark));
}

static gboolean
gwy_module_name_is_blocked(const gchar *modname)
{
    GHashTable *blocked_modules;
    GQuark quark;

    blocked_modules = gwy_module_get_blocking_table(FALSE);
    if (!blocked_modules)
        return FALSE;

    quark = g_quark_from_string(modname);

    return !!g_hash_table_lookup(blocked_modules, GSIZE_TO_POINTER(quark));
}

static GHashTable*
gwy_module_get_blocking_table(gboolean do_create)
{
    static GHashTable *blocked_modules = NULL;

    if (!blocked_modules && do_create)
        blocked_modules = g_hash_table_new(g_direct_hash, g_direct_equal);

    return blocked_modules;
}

/************************** Documentation ****************************/

/**
 * SECTION:gwymoduleenums
 * @title: gwymoduleenums
 * @short_description: Common enumerations
 **/

/**
 * SECTION:gwymoduleloader
 * @title: gwymoduleloader
 * @short_description: Basic module loader interface
 **/

/**
 * GWY_MODULE_ABI_VERSION:
 *
 * Gwyddion module ABI version.
 *
 * To be filled as @abi_version in #GwyModuleInfo.
 **/

/**
 * GWY_MODULE_QUERY:
 * @mod_info: The #GwyModuleInfo structure to return as module info.
 *
 * The module query must be the ONLY exported symbol from a module.
 *
 * This macro does The Right Thing necessary to export module info in a way Gwyddion understands it. Put
 * %GWY_MODULE_QUERY with the module info (#GwyModuleInfo) of your module as its argument on a line (with NO semicolon
 * after).
 *
 * If you write a module in C++ note the module query must have C linkage. This is achieved by marking it
 * <literal>extern "C"</literal>:
 * |[
 * extern "C" {
 * GWY_MODULE_QUERY(module_info)
 * }
 * ]|
 * This has to be done manually in versions up to 2.24; since version 2.25 * GWY_MODULE_QUERY() includes
 * <literal>extern "C"</literal> automatically if it is compiled using a C++ compiler.
 **/

/**
 * GWY_MODULE_QUERY2:
 * @mod_info: The #GwyModuleInfo structure to return as module info.
 * @mod_name: Module name, given as C identifier corresponding to file name withough extension.
 *
 * The module query must be the ONLY exported symbol from a module.
 *
 * See %GWY_MODULE_QUERY for discussion.
 *
 * This macro is intended for modules that can be bundled together to one big shared library for time and space
 * optimization.  This mostly makes sense only for modules included in Gwyddion itself as it contains hundreds of
 * modules.
 *
 * However, it safe to use in third party modules because if modules are not bundled the macro behaves exactly as
 * %GWY_MODULE_QUERY and the @mod_name argument is just ignored.  The macro expansion differs only when
 * %GWY_MODULE_BUNDLING is defined when including the "gwymoduleloader.h" header.
 *
 * The module name @mod_name is given as a C identifier, not a string.  It <emphasis>must</emphasis> be identical to
 * the file name of the module if it was installed standalone, with extension removed and with non-identifier
 * characters converted to underscores.  For instance module implemented in "nt-mdt.c" and installed as "nt-mdt.so" or
 * "nt-mdt.dll" (or with other extensions, depending on the operating system) must pass <literal>nt_mdt</literal>.  If
 * it does not match the file name, linking will fail with a confusing message when bundling is enabled.
 *
 * Since: 2.49
 **/

/**
 * GWY_MODULE_BUNDLE_FLAG
 *
 * Value to bitwise combine with %GWY_MODULE_ABI_VERSION to indicate a bundle.
 *
 * Since: 2.49
 **/

/**
 * GwyModuleRegisterFunc:
 *
 * Module registration function type.
 *
 * It actually runs particular featrue registration functions, like gwy_file_func_register() and
 * gwy_process_func_register().
 *
 * If the module has a complex initialisation it may be safer to simply not register any function but return %TRUE
 * even if it fails to set up itself correctly.  The module feature unregistration is somewhat crude and, generally,
 * unregisteration may lead to disaster when shared library unloading has unexpected side effects.
 *
 * Returns: Whether the registration succeeded.  When it returns %FALSE, the module and its features are unregistered.
 **/

/**
 * GwyModuleQueryFunc:
 *
 * Module query function type.
 *
 * The module query function should be simply declared as GWY_MODULE_QUERY(mod_info), where mod_info is module info
 * struct for the module.
 *
 * Returns: The module info struct.
 **/

/**
 * GwyModuleBundleRegisterFunc:
 *
 * Module bundle query function type.
 *
 * It returns an array of module records for all modules in the bundle, terminated by {%NULL, %NULL}.
 *
 * Since: 2.49
 **/

/**
 * GwyModuleInfo:
 * @abi_version: Gwyddion module ABI version, should be always #GWY_MODULE_ABI_VERSION.
 * @register_func: Module registration function (the function run by Gwyddion module system, actually registering
 * particular module features).
 * @blurb: Some module description.
 * @author: Module author(s).
 * @version: Module version.
 * @copyright: Who has copyright on this module.
 * @date: Date (year).
 *
 * Module information returned by GWY_MODULE_QUERY().
 **/

/**
 * GwyModuleFailureInfo:
 * @filename: Name of the file the module was loaded from.
 * @modname: Module name (can be %NULL and contain odd bytes).
 * @err_message: Error message from the failed module registration.
 * @err_domain: #GError domain from the failed module registration.
 * @err_code: #GError code from the failed module registration.
 *
 * Information about a failed module registration.
 *
 * Since: 2.49
 **/

/**
 * GwyModuleRecord:
 * @query: Module query function.
 * @name: Module name (base file name without extensions).
 *
 * Module record returned by bundle query function.
 *
 * Since: 2.49
 **/

/**
 * GwyRunType:
 * @GWY_RUN_NONE: None.
 * @GWY_RUN_NONINTERACTIVE: The function is run non-interactively, it must not present any GUI and touch application
 *                          state.
 * @GWY_RUN_INTERACTIVE: The function presents a modal user interfaces where parameters can be adjusted, it returns
 *                       after finishing all operations.
 * @GWY_RUN_IMMEDIATE: Function is run immediately and uses parameter values stored in the settings to reproduce
 *                     previous run. It can however present GUI error messages or display progress, create new
 *                     windows, etc.
 * @GWY_RUN_MASK: The mask for all the run modes.
 *
 * Module function run-modes.
 *
 * Note @GWY_RUN_NONINTERACTIVE is only possible for file functions, processing functions do not have a truly
 * non-interactive interface yet and therefore they should not claim they support it.  The closest match for process
 * functions is @GWY_RUN_IMMEDIATE.
 **/

/**
 * GwyModuleError:
 * @GWY_MODULE_ERROR_NAME: Module has an invalid name.  It is recommended that module names are valid C identifiers,
 *                         possibly with dashes instead of underscores, but only really broken names are rejected.
 * @GWY_MODULE_ERROR_DUPLICATE: A module of the same name has already been registered.
 * @GWY_MODULE_ERROR_OPEN: Calling g_module_open() on the module failed.
 * @GWY_MODULE_ERROR_QUERY: Module does not contain any query function.
 * @GWY_MODULE_ERROR_ABI: Module has different ABI version than expected/supported; or required info field are
 *                        missing.
 * @GWY_MODULE_ERROR_INFO: Module query function provided %NULL info.
 * @GWY_MODULE_ERROR_REGISTER: The registration function returned %FALSE; or the module did not register any function.
 * @GWY_MODULE_ERROR_NESTING: Nested module bundle found.  (Since 2.49)
 *
 * Type of module loading and registration error.
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
