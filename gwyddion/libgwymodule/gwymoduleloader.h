/*
 *  $Id: gwymoduleloader.h 24728 2022-03-23 16:02:56Z yeti-dn $
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

#ifndef __GWY_MODULE_LOADER_H__
#define __GWY_MODULE_LOADER_H__

#include <gmodule.h>

G_BEGIN_DECLS

#define GWY_MODULE_ABI_VERSION 2
#define GWY_MODULE_BUNDLE_FLAG 256u

#ifdef  __cplusplus
#define __GWY_MODULE_QUERY_EXTERN_C_START extern "C" {
#define __GWY_MODULE_QUERY_EXTERN_C_END }
#else
#define __GWY_MODULE_QUERY_EXTERN_C_START /* */
#define __GWY_MODULE_QUERY_EXTERN_C_END /* */
#endif

#define GWY_MODULE_QUERY(mod_info) \
    __GWY_MODULE_QUERY_EXTERN_C_START \
        G_MODULE_EXPORT GwyModuleInfo* _gwy_module_query(void) { return &mod_info; } \
    __GWY_MODULE_QUERY_EXTERN_C_END

#ifdef GWY_MODULE_BUNDLING
#define GWY_MODULE_QUERY2(mod_info,mod_name) \
    __GWY_MODULE_QUERY_EXTERN_C_START \
        G_GNUC_INTERNAL GwyModuleInfo* _gwy_module_query__##mod_name(void) { return &mod_info; } \
    __GWY_MODULE_QUERY_EXTERN_C_END
#else
#define GWY_MODULE_QUERY2(mod_info,mod_name) GWY_MODULE_QUERY(mod_info)
#endif

#define GWY_MODULE_ERROR gwy_module_error_quark()

typedef enum {
    GWY_MODULE_ERROR_NAME,
    GWY_MODULE_ERROR_DUPLICATE,
    GWY_MODULE_ERROR_OPEN,
    GWY_MODULE_ERROR_QUERY,
    GWY_MODULE_ERROR_ABI,
    GWY_MODULE_ERROR_INFO,
    GWY_MODULE_ERROR_REGISTER,
    GWY_MODULE_ERROR_NESTING
} GwyModuleError;

typedef struct _GwyModuleInfo GwyModuleInfo;
typedef struct _GwyModuleRecord GwyModuleRecord;

typedef gboolean               (*GwyModuleRegisterFunc)       (void);
typedef GwyModuleInfo*         (*GwyModuleQueryFunc)          (void);
typedef const GwyModuleRecord* (*GwyModuleBundleRegisterFunc) (void);

struct _GwyModuleRecord {
    GwyModuleQueryFunc query;
    const gchar *name;
};

struct _GwyModuleInfo {
    guint32 abi_version;
    GwyModuleRegisterFunc register_func;
    const gchar *blurb;
    const gchar *author;
    const gchar *version;
    const gchar *copyright;
    const gchar *date;
};

typedef struct {
    const gchar *filename;
    const gchar *modname;
    const gchar *err_message;
    gint err_domain;
    gint err_code;
} GwyModuleFailureInfo;

GQuark               gwy_module_error_quark         (void);
void                 gwy_module_register_modules    (const gchar **paths);
const GwyModuleInfo* gwy_module_lookup              (const gchar *name);
const gchar*         gwy_module_get_filename        (const gchar *name);
GSList*              gwy_module_get_functions       (const gchar *name);
void                 gwy_module_foreach             (GHFunc function,
                                                     gpointer data);
void                 gwy_module_failure_foreach     (GFunc function,
                                                     gpointer data);
const GwyModuleInfo* gwy_module_register_module     (const gchar *name,
                                                     GError **error);
void                 gwy_module_disable_registration(const gchar *name);
void                 gwy_module_enable_registration (const gchar *name);
gboolean             gwy_module_is_enabled          (const gchar *name);

G_END_DECLS

#endif /* __GWY_MODULE_LOADER_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
