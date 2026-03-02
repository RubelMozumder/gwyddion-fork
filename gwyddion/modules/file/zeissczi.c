/*
 *  $Id: zeissczi.c 25135 2022-11-02 14:01:25Z yeti-dn $
 *  Copyright (C) 2022 David Necas (Yeti), Daniil Bratashov (dn2010)
 *  E-mail: dn2010@gwyddion.net.
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

/*
 * Based on ZISRAW (CZI) File Format
 * Design specification
 * V 1.2.2
 * Date of publication: 12. July 2016
 * officially obtained from Carl Zeiss Microscopy GmbH
 */


/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-zeiss-czi-spm">
 *   <comment>Carl Zeiss CZI images</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="ZISRAWFILE"/>
 *   </magic>
 *   <glob pattern="*.czi"/>
 *   <glob pattern="*.CZI"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Zeiss CZI
 * 0 string ZISRAWFILE Carl Zeiss CZI images
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Carl Zeiss CZI images
 * .czi
 *
 **/


#include "config.h"
#include <stdlib.h>
#include <libgwyddion/gwymath.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>
#include "err.h"

#define MAGIC "ZISRAWFILE"
#define MAGIC_SIZE (10)

#define EXTENSION ".czi"

typedef struct {
    gchar id[16];
    guint64 allocatedsize;
    guint64 usedsize;
    const guchar *data;
} CziSegment;

typedef struct {
    gint32 major; /* 1 */
    gint32 minor; /* 0 */
    gint32 reserved1;
    gint32 reserved2;
    gchar  primaryfileguid[16];
    gchar  fileguid[16];
    gint32 filepart;
    gint64 directoryposition;
    gint64 metadataposition;
    gint32 updatepending;
    gint64 attachmentdirectoryposition;
} CziFileHeader;

typedef struct {
    gint32 xmlsize;
    gint32 attachmentsize; /* not used */
    const guchar *xmldata;
} CziMetadataSegment;

typedef struct {
    gchar  dimension[4];
    gint32 start;
    gint32 size;
    gfloat startcoordinate;
    gint32 storedsize;
} CziDimensionEntry;

typedef struct {
    gchar  schematype[2];
    gint32 pixeltype;
    gint64 fileposition;
    gint32 filepart; /* reserved */
    gint32 compression;
    gchar  pyramidtype;
    gchar  spare[5];
    gint32 dimensioncount;
    CziDimensionEntry **dimensionentries;
} CziDirectoryEntry;

typedef struct {
    gint32 metadatasize;
    gint32 attachmentsize;
    gint64 datasize;
    CziDirectoryEntry *direntry;
    const guchar *metadata;
    const guchar *data;
    const guchar *attachments;
} CziRawSubBlock;

static gboolean           module_register       (void);
static gint               czi_detect            (const GwyFileDetectInfo *fileinfo,
                                                 gboolean only_name);
static GwyContainer*      czi_load              (const gchar *filename,
                                                 GwyRunType mode,
                                                 GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    module_register,
    N_("Imports Carl Zeiss CZI images."),
    "Daniil Bratashov <dn2010@gwyddion.net>",
    "0.0",
    "Daniil Bratashov (dn2010), David Nečas (Yeti)",
    "2022",
};

GWY_MODULE_QUERY2(module_info, zeissczi)

static gboolean
module_register(void)
{
    gwy_file_func_register("zeissczi",
                           N_("Carl Zeiss CZI images (.czi)"),
                           (GwyFileDetectFunc)&czi_detect,
                           (GwyFileLoadFunc)&czi_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
czi_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    gint score = 0;

    if (only_name)
        return (g_str_has_suffix(fileinfo->name_lowercase, EXTENSION))
                ? 20 : 0;

    if (fileinfo->buffer_len > MAGIC_SIZE
        && memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) == 0)
        score = 100;

    return score;
}

static GwyContainer*
czi_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyContainer *container = NULL;
    gchar *buffer = NULL;
    gsize size = 0;
    GError *err = NULL;    
    const guchar *p;

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        goto fail;
    }

    if (size < 10 + 512) { /* header too short */
        err_TOO_SHORT(error);
        goto fail;
    }

    p = buffer;

fail:
	g_free(buffer);

    return container;
}
