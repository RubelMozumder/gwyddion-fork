/*
 *  $Id: robotics.c 26153 2024-02-05 12:28:41Z yeti-dn $
 *  Copyright (C) 2011 Miroslav Valtr (Mira).
 *  E-mail: miraval@seznam.cz.
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
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-robotics-spm">
 *   <comment>Automation and Robotics Dual Lens Mapper data</comment>
 *   <magic priority="50">
 *     <match type="string" offset="0" value="File version:"/>
 *   </magic>
 *   <glob pattern="*.mcr"/>
 *   <glob pattern="*.MCR"/>
 *   <glob pattern="*.mct"/>
 *   <glob pattern="*.MCT"/>
 *   <glob pattern="*.mce"/>
 *   <glob pattern="*.MCE"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Robotics
 * # Quite unspecific.
 * 0 string File\ version:\x090 Automation and Robotics Dual Lens Mapper data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Automation and Robotics Dual Lens Mapper
 * .mcr, .mct, .mce
 * Read
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/data-browser.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"

#define MAGIC "File version:\t0"
#define MAGIC_SIZE (sizeof(MAGIC)-1)
#define EXTENSION_MCR ".mcr"
#define EXTENSION_MCT ".mct"
#define EXTENSION_MCE ".mce"
#define NUM_DFIELDS 14

static gboolean      module_register      (void);
static gint          robotics_detect      (const GwyFileDetectInfo *fileinfo,
                                           gboolean only_name);
static GwyContainer* robotics_load        (const gchar *filename,
                                           G_GNUC_UNUSED GwyRunType mode,
                                           GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Automation & Robotics Dual Lensmapper data files"),
    "Mira <miraval@seznam.cz>",
    "0.2",
    "Miroslav Valtr",
    "2011",
};

GWY_MODULE_QUERY2(module_info, robotics)

static gboolean
module_register(void)
{
    gwy_file_func_register("robotics",
                           N_("Dual Lensmapper files"),
                           (GwyFileDetectFunc)&robotics_detect,
                           (GwyFileLoadFunc)&robotics_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
robotics_detect(const GwyFileDetectInfo *fileinfo,
                gboolean only_name)
{
    if (only_name) {
        if (g_str_has_suffix(fileinfo->name_lowercase, EXTENSION_MCR)
            || g_str_has_suffix(fileinfo->name_lowercase, EXTENSION_MCT)
            || g_str_has_suffix(fileinfo->name_lowercase, EXTENSION_MCE))
            return 10;
        else
            return 0;
    }

    if (fileinfo->file_size < MAGIC_SIZE + 2 || memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) != 0)
        return 0;

    return 50;
}

static GwyContainer*
robotics_load(const gchar *filename,
              G_GNUC_UNUSED GwyRunType mode,
              GError **error)
{
    static const gchar *channel_titles[NUM_DFIELDS] = {
        "PosX", "PosY", "Dpt", "Sph", "Cyl", "Axis", "Valid",
        "NormX", "NormY", "NormZ", "PosZ", "MinCurvX", "MinCurvY", "MinCurvZ",
    };
    GwyContainer *container = NULL, *meta;
    GError *err = NULL;
    gsize size = 0;
    gchar *line, *p, *buffer = NULL, *comment = NULL;
    const gchar *s;
    guint version, origin, i, j, xres, yres;
    gint valid;
    gchar **split_line = NULL;
    GwyDataField **dfield = NULL;
    gdouble xreal, yreal, xoffset, yoffset;
    gdouble **data = NULL;

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    p = buffer;
    line = gwy_str_next_line(&p);
    if (!line || sscanf(line, "File version:\t%u", &version) != 1) {
        err_FILE_TYPE(error, "Automation & Robotics");
        goto fail;
    }

    line = gwy_str_next_line(&p);
    split_line = g_strsplit(line, "\t", 2);
    if (!line || g_strv_length(split_line) < 2) {
        err_MISSING_FIELD(error, "Comment");
        goto fail;
    }
    comment = g_strdup(split_line[1]);
    g_strfreev(split_line);

    line = gwy_str_next_line(&p);
    if (!line || sscanf(line, "Carto origin (0=Refl, 1=Transm, 2=Extern):\t%u", &origin) != 1) {
        err_MISSING_FIELD(error, "Origin");
        goto fail;
    }

    for (i = 1; i < 11; i++)
         line = gwy_str_next_line(&p);

    line = gwy_str_next_line(&p);
    if (!line
        || sscanf(line, "Nbs Points (x,y):\t%u\t%u", &xres, &yres) != 2) {
        err_MISSING_FIELD(error, "Nbs Points (x,y)");
        goto fail;
    }

    line = gwy_str_next_line(&p);
    split_line = g_strsplit(line, "\t", 3);
    if (!line || g_strv_length(split_line) < 3) {
        err_MISSING_FIELD(error, "Size (x,y in mm)");
        goto fail;
    }
    //0.001 is the conversion factor from mm to m
    xreal = 0.001*g_ascii_strtod(split_line[1], NULL);
    yreal = 0.001*g_ascii_strtod(split_line[2], NULL);
    g_strfreev(split_line);

    line = gwy_str_next_line(&p);

    line = gwy_str_next_line(&p);
    split_line = g_strsplit(line, "\t", NUM_DFIELDS+1);
    /* FIXME: Maybe the fail condition can be less strict or generally differently formulated. I am basing it on what
     * fields the code below tries to access. */
    if (!line || g_strv_length(split_line) < MAX(7, NUM_DFIELDS)) {
        err_FILE_TYPE(error, "Automation & Robotics");
        goto fail;
    }
    //0.001 is the conversion factor from mm to m
    xoffset = 0.001*g_ascii_strtod(split_line[0], NULL);
    yoffset = 0.001*g_ascii_strtod(split_line[1], NULL);
    valid = g_ascii_strtod(split_line[6], NULL);

    dfield = g_new0(GwyDataField*, NUM_DFIELDS);
    data = g_new0(gdouble*, NUM_DFIELDS);
    for (i = 0; i < NUM_DFIELDS; i++) {
         dfield[i] = gwy_data_field_new(xres, yres, xreal, yreal, TRUE);
         data[i] = gwy_data_field_get_data(dfield[i]);
         gwy_data_field_set_xoffset(dfield[i], xoffset);
         gwy_data_field_set_yoffset(dfield[i], yoffset);
         gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield[i]), "m");
    }

    if (valid) {
        for (i = 0; i < NUM_DFIELDS; i++) {
            data[i][0] = g_ascii_strtod(split_line[i], NULL);
        }
    }
    g_strfreev(split_line);

    for (j = 1; j < xres*yres; j++) {
        line = gwy_str_next_line(&p);
        if (!line) {
            err_TOO_SHORT(error);
            goto fail;
        }
        split_line = g_strsplit(line, "\t", NUM_DFIELDS+1);
        if (g_strv_length(split_line) < MAX(7, NUM_DFIELDS)) {
            err_FILE_TYPE(error, "Automation & Robotics");
            goto fail;
        }
        valid = g_ascii_strtod(split_line[6], NULL);
        if (valid) {
            for (i = 0; i < NUM_DFIELDS; i++)
                data[i][j] = g_ascii_strtod(split_line[i], NULL);
        }
        g_strfreev(split_line);
    }

    container = gwy_container_new();
    for (i = 0; i < NUM_DFIELDS; i++) {
        gwy_container_pass_object(container, gwy_app_get_data_key_for_id(i), dfield[i]);
        gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(i), channel_titles[i]);
        gwy_file_channel_import_log_add(container, i, NULL, filename);
    }

    meta = gwy_container_new();

    //gwy_container_set_string_by_name(meta, "File version:", g_strdup_printf("%d", version));
    gwy_container_set_string_by_name(meta, "Comment:", comment);

    if ((s = gwy_enuml_to_string(origin, "Refl", 0, "Transm", 1, "Extern", 2, NULL)))
        gwy_container_set_const_string_by_name(meta, "Carto origin:", s);

    gwy_container_set_string_by_name(meta, "Nbs Points (x,y):",
                                     g_strdup_printf("%u,%u", xres, yres));
    gwy_container_set_string_by_name(meta, "Size (x,y in mm):",
                                     g_strdup_printf("%.3lf,%.3lf", xreal*1000.0, yreal*1000.0));

    /* XXX: What about the other channels? */
    gwy_container_pass_object(container, gwy_app_get_data_meta_key_for_id(0), meta);

fail:
    g_free(buffer);
    g_free(dfield);
    g_free(data);

    return container;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
