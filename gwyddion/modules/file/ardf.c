/*
 *  $Id: ardf.c 26268 2024-03-30 16:41:51Z vojtakl4 $
 *  Copyright (C) Vojtech Klapetek, Matt Poss
 *  E-mail: vojvojta@gmail.com
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
 * <mime-type type="application/x-ardf-spm">
 *   <comment>Asylum Research ARDF SPM data</comment>
 *   <magic priority="60">
 *     <match type="string" offset="8" value="ARDF"/>
 *   </magic>
 *   <glob pattern="*.ardf"/>
 *   <glob pattern="*.ARDF"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Asylum Research ARDF
 * .ardf
 * Read Volume
 **/

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <glib/gstdio.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <libprocess/brick.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/wait.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"
#include "get.h"

#define EXTENSION ".ARDF"

#define MAGIC "ARDF"
#define MAGIC_SIZE (sizeof(MAGIC) - 1)

enum { HEADER_SIZE = 912 };   // based on two files

guchar TYPE_ARDF[4] = { 'A', 'R', 'D', 'F' };
guchar TYPE_FTOC[4] = { 'F', 'T', 'O', 'C' };
guchar TYPE_TTOC[4] = { 'T', 'T', 'O', 'C' };
guchar TYPE_IMAG[4] = { 'I', 'M', 'A', 'G' };
guchar TYPE_VOLM[4] = { 'V', 'O', 'L', 'M' };
guchar TYPE_NEXT[4] = { 'N', 'E', 'X', 'T' };
guchar TYPE_NSET[4] = { 'N', 'S', 'E', 'T' };
guchar TYPE_THMB[4] = { 'T', 'H', 'M', 'B' };
guchar TYPE_TOFF[4] = { 'T', 'O', 'F', 'F' };
guchar TYPE_IDAT[4] = { 'I', 'D', 'A', 'T' };
guchar TYPE_VOFF[4] = { 'V', 'O', 'F', 'F' };
guchar TYPE_IBOX[4] = { 'I', 'B', 'O', 'X' };
guchar TYPE_VTOC[4] = { 'V', 'T', 'O', 'C' };
guchar TYPE_VSET[4] = { 'V', 'S', 'E', 'T' };
guchar TYPE_IDEF[4] = { 'I', 'D', 'E', 'F' };
guchar TYPE_VDEF[4] = { 'V', 'D', 'E', 'F' };
guchar TYPE_TEXT[4] = { 'T', 'E', 'X', 'T' };
guchar TYPE_VNAM[4] = { 'V', 'N', 'A', 'M' };
guchar TYPE_VDAT[4] = { 'V', 'D', 'A', 'T' };
guchar TYPE_XDAT[4] = { 'X', 'D', 'A', 'T' };
guchar TYPE_GAMI[4] = { 'G', 'A', 'M', 'I' };
guchar TYPE_VCHN[4] = { 'V', 'C', 'H', 'N' };
guchar TYPE_MLOV[4] = { 'M', 'L', 'O', 'V' };
guchar TYPE_XDEF[4] = { 'X', 'D', 'E', 'F' };
guchar TYPE_XXXX[4] = { '\0', '\0', '\0', '\0' };

typedef struct {
    int ble;
} ARDFFile;

typedef struct {
    guint32 check_CRC32;
    guint32 size_bytes;
    guchar type_pnt[4];
    guint32 misc_num;
} ARDFPointer;

typedef struct {
    guint64 size_table;
    guint32 numb_entry;
    guint32 size_entry;
    guint64 *pnt_imag; //FTOC, IMAG, VOLM
    guint32 len_imag;
    guint64 *pnt_volm; //FTOC, IMAG, VOLM
    guint32 len_volm;
    guint64 *pnt_next; //FTOC, IMAG, VOLM
    guint64 *pnt_nset; //FTOC, IMAG, VOLM
    guint64 *pnt_thmb; //FTOC, IMAG, VOLM
    guint64 *idx_text; //TTOC
    guint64 *pnt_text; //TTOC
    guint32 len_text;
    guint32 *pnt_counter; //VOFF
    guint32 *lin_counter; //VOFF
    guint64 *lin_pointer; //VOFF
    gfloat *data; //IDAT
    guint32 len_data;
    guint32 x_size;
    guint32 y_size;
} ARDF_TOC;

typedef struct {
    guint32 force;
    guint32 line;
    guint32 point;
    guint64 prev;
    guint64 next;
} ARDF_VSET;

typedef struct {
    guint32 points;
    guint32 lines;
    guchar image_title[32];
} ARDF_DEF;

typedef struct {
    guint32 force;
    guint32 line;
    guint32 point;
    guint32 size_text;
    guchar* name;
} ARDF_VNAM;

typedef struct {
    guint32 force;
    guint32 line;
    guint32 point;
    guint32 size_data;
    guint32 force_type;
    guint32 pnt0;
    guint32 pnt1;
    guint32 pnt2;
    gfloat* data;
} ARDF_VDAT;

typedef struct {
    guchar* text;
    guint64 len;
} ARDF_TEXT;

static gboolean      module_register    (void);
static gint          ardf_detect        (const GwyFileDetectInfo *fileinfo,
                                         gboolean only_name);
static GwyContainer* ardf_load          (const gchar *filename,
                                         GwyRunType mode,
                                         GError **error);
static void          err_HEADER_MISMATCH(GError **error,
                                         const gchar *found,
                                         const gchar *expected);
static ARDFPointer*  ardf_read_pointer  (guchar **pp,
                                         guchar *start,
                                         gsize size,
                                         gint32 address,
                                         GError **error);
static gboolean      check_type         (guchar expected[4],
                                         guchar found[4],
                                         GError **error);
static ARDF_TOC*     read_ARDF_TOC      (guchar **pp,
                                         guchar *start,
                                         gsize size,
                                         gint32 address,
                                         guchar pointer_type[4],
                                         GError **error);
static ARDF_VSET*    read_ARDF_VSET     (guchar **pp,
                                         guchar *start,
                                         gsize size,
                                         gint32 address,
                                         GError **error);
static ARDF_DEF*     read_ARDF_DEF      (guchar **pp,
                                         guchar *start,
                                         gsize size,
                                         gint32 address,
                                         guchar pointer_type[4],
                                         GError **error);
static ARDF_TEXT*    read_ARDF_TEXT     (guchar **pp,
                                         guchar *start,
                                         gsize size,
                                         gint32 address,
                                         GError **error);
static ARDF_VNAM*    read_ARDF_VNAM     (guchar **pp,
                                         guchar *start,
                                         gsize size,
                                         gint32 address,
                                         GError **error);
static ARDF_VDAT*    read_ARDF_VDAT     (guchar **pp,
                                         guchar *start,
                                         gsize size,
                                         gint32 address,
                                         GError **error);
static gboolean      read_XDAT          (guchar **pp,
                                         guchar *start,
                                         gsize size,
                                         gint32 address,
                                         GError **error);
static GwyContainer* parse_metadata     (ARDF_TEXT *raw);
static void          free_ARDF_TOC      (ARDF_TOC *toc);
static void          free_ARDF_TEXT     (ARDF_TEXT *txt);
static void          free_ARDF_VNAM     (ARDF_VNAM *vnam);
static void          free_ARDF_VDAT     (ARDF_VDAT *vdat);


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Asylum Research ARDF files."),
    "Vojtech Klapetek <vojvojta@gmail.com>",
    "0.1",
    "Vojtech Klapetek, Matt Poss",
    "2023",
};

GWY_MODULE_QUERY2(module_info, ardf)

static gboolean
module_register(void)
{
    gwy_file_func_register("ardf",
                           N_("ARDF file (.ARDF)"),
                           (GwyFileDetectFunc)&ardf_detect,
                           (GwyFileLoadFunc)&ardf_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
ardf_detect(const GwyFileDetectInfo *fileinfo,
            gboolean only_name)
{
    if (only_name)
        return g_str_has_suffix(fileinfo->name, EXTENSION) ? 50 : 0;

    if (fileinfo->buffer_len > MAGIC_SIZE + 8   //magic is on 0x08
        && memcmp(fileinfo->head + 8, MAGIC, MAGIC_SIZE) == 0)
        return 100;

    return 0;
}

static GwyContainer*
ardf_load(const gchar *filename,
          G_GNUC_UNUSED GwyRunType mode,
          GError **error)
{
    //ARDFFile ardf;
    ARDFPointer *apoint;
    ARDF_TOC *ftoc = NULL;
    ARDF_TOC *ttoc = NULL;
    ARDF_TOC *imtoc = NULL;
    ARDF_TOC *imttoc = NULL;
    ARDF_TOC *idat = NULL;
    ARDF_TOC *volm = NULL;
    ARDF_TOC *volm_ttoc = NULL;
    ARDF_TOC *idx = NULL;
    ARDF_DEF *imdef = NULL;
    ARDF_DEF *vdef = NULL;
    GwyContainer *container = NULL;
    GwyContainer *instrument_data = NULL;
    GwyContainer *meta = NULL;
    GwyDataField *dfield = NULL;
    GwyBrick **volm_channels = NULL;
    guchar *buffer, *b, *p;
    const guchar *cb;
    guchar *start;    // is supposed to be const but would cause problems
    gsize size;
    GError *err = NULL;
    gint64 dist_to_TTOC;
    guint numb_notes, numb_imag_text, numb_volm, numb_channels = 0;
    guint i, j, c_vchn = 0, size_table, c, c_volm, point_first;
    guint64 loc_imag_ttoc, loc_imag_idef;
    guint64 numb_points, numb_lines, r, loc_line, adj_line, n, data_len, nplane;
    guint *numb_force, *numb_line, *numb_point;
    guint64 *loc_prev, *loc_next;
    guchar *volm_channel = NULL;
    //guchar *xdef_text;
    guchar **vchn = NULL;
    GString *channel_name = NULL;
    //gboolean trace;
    gboolean done, scan_down, snake_like;
    gboolean wait_dialogue_shown = FALSE;
    gdouble lat_size;
    gdouble *volm_dfield;
    ARDF_VSET *vset = NULL;
    ARDF_VNAM *vnam = NULL;
    ARDF_VDAT *vdat = NULL;
    ARDF_TEXT *txt = NULL;
    ARDF_TEXT *raw_note = NULL;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    if (size < HEADER_SIZE) {
        err_TOO_SHORT(error);
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    if (memcmp(buffer + 8, MAGIC, MAGIC_SIZE) != 0) {
        err_FILE_TYPE(error, "ARDF");
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    start = buffer; //saving the start of file
    b = buffer; //copying buffer to avoid dealocation issues

    gwy_app_wait_start(NULL, _("Reading images and metadata..."));
    wait_dialogue_shown = TRUE;

    apoint = ardf_read_pointer(&b, start, size, -1, error);
    if (apoint == NULL) {
        //err_FILE_TYPE(error, "ARDF");
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }
    if (!check_type(TYPE_ARDF, apoint->type_pnt, error))
        goto fail;

    ftoc = read_ARDF_TOC(&b, start, size, -1, TYPE_FTOC, error);
    if (ftoc == NULL) {
        //err_INVALID(error, "FTOC");
        goto fail;
    }
    gwy_debug("FTOC read\n\n");

    dist_to_TTOC = ftoc->size_table + 16;
    ttoc = read_ARDF_TOC(&b, start, size, dist_to_TTOC, TYPE_TTOC, error);
    if (ttoc == NULL) {
        //err_INVALID(error, "TTOC");
        goto fail;
    }
    gwy_debug("TTOC read\n\n");

    if ((ftoc->len_imag == 0) && (ftoc->len_volm == 0)) {
        err_NO_DATA(error);
        goto fail;
    }

    numb_notes = sizeof(ttoc->pnt_text)/8;
    //gwy_debug("numb notes: %li\n", numb_notes);
    if (numb_notes == 0) {
        err_MISSING_FIELD(error, "instrument data");
        goto fail;
    }
    if (numb_notes != 1) {
        gwy_debug("Multiple TEXT notes in file - not supported yet");
    }
    txt = read_ARDF_TEXT(&b, start, size, ttoc->pnt_text[0], error);
    if (txt == NULL) {
        //err_INVALID(error, "instrument data");
        goto fail;
    }
    instrument_data = parse_metadata(txt);

    lat_size = 10e-6;
    if (gwy_container_contains_by_name(instrument_data, "FastScanSize"))
        sscanf(gwy_container_get_string_by_name(instrument_data, "FastScanSize"), "%lf", &lat_size);
    else {
        gwy_debug("scan size not set, guessing 10e-6\n");
    }

    free_ARDF_TEXT(txt);
    txt = NULL;
    gwy_debug("instrument notes (kind of) read\n\n");

    container = gwy_container_new();

    gwy_debug("image count: %i\n", ftoc->len_imag);
    for (i = 0; i < ftoc->len_imag; i++) {
        imtoc = read_ARDF_TOC(&b, start, size, ftoc->pnt_imag[i], TYPE_IMAG, error);
        if (imtoc == NULL) {
            //err_INVALID(error, "IMAG TOC");
            goto fail;
        }
        loc_imag_ttoc = ftoc->pnt_imag[i] + imtoc->size_table;
        imttoc = read_ARDF_TOC(&b, start, size, loc_imag_ttoc, TYPE_TTOC, error);
        if (imttoc == NULL) {
            //err_INVALID(error, "IMAG TTOC");
            goto fail;
        }
        loc_imag_idef = loc_imag_ttoc + imttoc->size_table;
        imdef = read_ARDF_DEF(&b, start, size, loc_imag_idef, TYPE_IDEF, error);
        if (imdef == NULL) {
            //err_INVALID(error, "IMAG DEF");
            goto fail;
        }
        // image name is imdef->image_title
        gwy_debug("image title: %s", imdef->image_title);
        idat = read_ARDF_TOC(&b, start, size, -1, TYPE_IBOX, error);
        if (idat == NULL) {
            //err_INVALID(error, "IMAG IDAT");
            goto fail;
        }
        g_free(apoint);
        apoint = ardf_read_pointer(&b, start, size, -1, error);
        if (apoint == NULL) {
            //err_INVALID(error, "IMAG");
            goto fail;
        }
        if (!check_type(TYPE_GAMI, apoint->type_pnt, error)) {
            //err_INVALID(error, "IMAG");
            goto fail;
        }
        numb_imag_text = imtoc->len_text;
        for (j = 0; j < numb_imag_text; j++) {
            if ((numb_imag_text < 2) || j == 1) {
                // reads only one note, presumably the important one
                raw_note = read_ARDF_TEXT(&b, start, size, imtoc->pnt_text[j], error);
                if (raw_note == NULL) {
                    //err_INVALID(error, "IMAG note");
                    goto fail;
                }
                gwy_container_pass_object(container, gwy_app_get_data_meta_key_for_id(i), parse_metadata(raw_note));
                free_ARDF_TEXT(raw_note);
                raw_note = NULL;
            }
        }
        if (numb_imag_text == 0) {
            // if no note is present, adds instrument data instead
            gwy_container_set_object(container, gwy_app_get_data_meta_key_for_id(i), instrument_data);
        }

        gwy_debug("size %i %i\n", idat->x_size, idat->y_size);

        dfield = gwy_data_field_new(idat->x_size,
                                    idat->y_size,
                                    lat_size,
                                    lat_size,
                                    FALSE);
        gwy_convert_raw_data(idat->data, idat->x_size*idat->y_size,
                             1, GWY_RAW_DATA_FLOAT,
                             GWY_BYTE_ORDER_LITTLE_ENDIAN,
                             gwy_data_field_get_data(dfield),
                             1, 0);
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");
        if (strstr(imdef->image_title, "Force") != NULL)
            gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), "N");
        else if (strstr(imdef->image_title, "Adhesion") != NULL)
            gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), "N");
        else if (strstr(imdef->image_title, "Young") != NULL)
            gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), "Pa");
        else if (strstr(imdef->image_title, "Voltage") != NULL)
            gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), "V");
        else
            gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), "m");

        gwy_container_pass_object(container, gwy_app_get_data_key_for_id(i), dfield);
        gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(i), imdef->image_title);
        gwy_file_channel_import_log_add(container, i, NULL, filename);

        GWY_FREE(imdef);
        free_ARDF_TOC(imtoc);
        imtoc = NULL;
        free_ARDF_TOC(imttoc);
        imttoc = NULL;
        free_ARDF_TOC(idat);
        idat = NULL;
    }

    gwy_debug("images read\n");

    if (!gwy_app_wait_set_message(_("Reading volume data..."))) {
        err_INVALID(error, "set load message");
        goto fail;
    }

    numb_volm = ftoc->len_volm;
    c_volm = 0;
    for (i = 0; i < numb_volm; i++) {
        volm = read_ARDF_TOC(&b, start, size, ftoc->pnt_volm[i], TYPE_VOLM, error);
        if (volm == NULL) {
            //err_INVALID(error, "VOLM TOC");
            goto fail;
        }
        volm_ttoc = read_ARDF_TOC(&b, start, size,
                                  ftoc->pnt_volm[i] + volm->size_table,
                                  TYPE_TTOC, error);
        if (volm_ttoc == NULL) {
            //err_INVALID(error, "VOLM TTOC");
            goto fail;
        }
        vdef = read_ARDF_DEF(&b, start, size,
                             ftoc->pnt_volm[i] + volm->size_table + volm_ttoc->size_table,
                             TYPE_VDEF, error);
        if (vdef == NULL) {
            //err_INVALID(error, "VOLM DEF");
            goto fail;
        }
        p = b;
        done = FALSE;
        c_vchn = 0;
        while (!done) {
            g_free(apoint);
            apoint = ardf_read_pointer(&p, start, size, -1, error);
            if (!apoint) {
                //err_INVALID(error, "VOLM data");
                goto fail;
            }
            if (memcmp(TYPE_VCHN, apoint->type_pnt, 4) == 0) {
                c_vchn += 1;
                p += apoint->size_bytes - 16;
            }
            else if (memcmp(TYPE_XDEF, apoint->type_pnt, 4) == 0) {
                done = TRUE;
            }
            else {
                err_HEADER_MISMATCH(error, apoint->type_pnt, "VCHN/XDEF");
                //err_INVALID(error, "VCHN/XDEF");
                goto fail;
            }
        }
        vchn = g_new(guchar*, c_vchn);
        for (j = 0; j < c_vchn; j++) {
            vchn[j] = g_new(guchar, 32);
        }
        c_vchn = 0;
        done = FALSE;
        while (!done) {
            g_free(apoint);
            apoint = ardf_read_pointer(&b, start, size, -1, error);
            if (apoint == NULL) {
                //err_INVALID(error, "VOLM data");
                goto fail;
            }
            if (memcmp(TYPE_VCHN, apoint->type_pnt, 4) == 0) {
                cb = b;
                memcpy(vchn[c_vchn], cb, 32);
                gwy_debug("%s\n", vchn[c_vchn]);
                if (b - start + apoint->size_bytes - 16 > size) {
                    //printf("buffer overflow");
                    err_TRUNCATED_PART(error, "VOLM");
                    goto fail;
                }
                b += 32;
                c_vchn += 1;
                b += apoint->size_bytes - 48;
            }
            else if (memcmp(TYPE_XDEF, apoint->type_pnt, 4) == 0) {
                if (b - start + apoint->size_bytes - 16 > size) {
                    //printf("buffer overflow");
                    err_TRUNCATED_PART(error, "VOLM");
                    goto fail;
                }
                b += 4;
                cb = b;
                size_table = gwy_get_guint32_le(&cb);
                b += 4;
                /*xdef_text = g_new(guchar, size_table + 1);
                get_CHARS(xdef_text, &b, size_table);
                xdef_text[size_table] = '\0';
                printf("%s\n", xdef_text);
                g_free(xdef_text);*/ //works, but probably useless
                b += size_table;
                b += apoint->size_bytes - 24 - size_table;
                done = TRUE;
            }
            else {
                //printf("%s not recognized\n", apoint->type_pnt);
                err_HEADER_MISMATCH(error, apoint->type_pnt, "VCHN/XDEF");
                goto fail;
            }
        }

        idx = read_ARDF_TOC(&b, start, size, -1, TYPE_VTOC, error);
        if (idx == NULL) {
            //err_INVALID(error, "VOLM VTOC");
            goto fail;
        }
        g_free(apoint);
        apoint = ardf_read_pointer(&b, start, size, -1, error);
        if (apoint == NULL) {
            //err_INVALID(error, "VOLM");
            goto fail;
        }
        if (!check_type(TYPE_MLOV, apoint->type_pnt, error)) {
            //err_INVALID(error, "VOLM");
            goto fail;
        }

        gwy_debug("volm info read");

        //read VOLM data
        numb_points = vdef->points;
        numb_lines = vdef->lines;
        numb_channels = c_vchn;
        nplane = numb_points*numb_lines;

        p = b;
        data_len = 0;
        scan_down = FALSE;
        //trace = FALSE;
        snake_like = FALSE;
        point_first = 0;
        for (r = 0; r < numb_lines; r++) {
            loc_line = idx->lin_pointer[r];
            //gwy_debug("%li\n", loc_line);
            if (loc_line != 0) {
                p = start + loc_line;
                for (n = 0; n < numb_points; n++) {
                    //gwy_debug("%li\n", n);
                    vset = read_ARDF_VSET(&p, start, size, -1, error);
                    if (vset == NULL) {
                        //err_INVALID(error, "VOLM VSET");
                        goto fail;
                    }
                    if (n == 0) {
                        scan_down = (vset->line != r);
                        /*if (vset->point == 0) trace = TRUE;
                        else trace = FALSE;*/  // what is trace good for?
                    }
                    if (r == 0 && n == 0) {
                        point_first = vset->point;
                    }
                    if (r == 1 && n == 0) {
                        if (vset->point != point_first) {
                            snake_like = TRUE;
                            gwy_debug("snakelike");
                        }
                        else {
                            gwy_debug("not snakelike");
                        }
                    }

                    vnam = read_ARDF_VNAM(&p, start, size, -1, error);
                    if (vnam == NULL) {
                        //err_INVALID(error, "VOLM VNAM");
                        goto fail;
                    }
                    for (c = 0; c < numb_channels; c++) {
                        vdat = read_ARDF_VDAT(&p, start, size, -1, error);
                        if (vdat == NULL) {
                            //err_INVALID(error, "VOLM VDAT");
                            goto fail;
                        }
                        if (vdat->size_data > data_len)
                            data_len = vdat->size_data;
                        free_ARDF_VDAT(vdat);
                        vdat = NULL;
                    }
                    if (!read_XDAT(&p, start, size, -1, error)) {
                        //err_INVALID(error, "VOLM XDAT");
                        goto fail; //read in each point, not as in real reading
                    }
                    GWY_FREE(vset);
                    free_ARDF_VNAM(vnam);
                    vnam = NULL;
                }
            }
        }

        volm_channels = g_new(GwyBrick*, numb_channels);
        for (j = 0; j < numb_channels; j++) {
            volm_channels[j] = gwy_brick_new(numb_points, numb_lines, data_len,
                                             lat_size, lat_size, 1,
                                             TRUE);
            gwy_si_unit_set_from_string(gwy_brick_get_si_unit_x(volm_channels[j]), "m");
            gwy_si_unit_set_from_string(gwy_brick_get_si_unit_y(volm_channels[j]), "m");
            gwy_si_unit_set_from_string(gwy_brick_get_si_unit_z(volm_channels[j]), "m");
            gwy_si_unit_set_from_string(gwy_brick_get_si_unit_w(volm_channels[j]), "m");
        }

        for (r = 0; r < numb_lines; r++) {
            if (scan_down)
                adj_line = numb_lines - r;
            else
                adj_line = r;

            loc_line = idx->lin_pointer[adj_line];
            //gwy_debug("loc line %li\n", loc_line);

            if (loc_line != 0) {
                b = start + loc_line;
                gwy_debug("line %li/%li\n",  r + 1, numb_lines);
                numb_force = g_new(guint32, numb_points);
                numb_line = g_new(guint32, numb_points);
                numb_point = g_new(guint32, numb_points);
                loc_prev = g_new(guint64, numb_points);
                loc_next = g_new(guint64, numb_points);

                for (n = 0; n < numb_points; n++) {
                    //gwy_debug("%li\n", n);
                    vset = read_ARDF_VSET(&b, start, size, -1, error);
                    if (vset == NULL) {
                        //err_INVALID(error, "VOLM VSET");
                        goto fail;
                    }

                    numb_force[n] = vset->force;
                    numb_line[n] = vset->line;
                    numb_point[n] = vset->point;
                    loc_prev[n] = vset->prev;
                    loc_next[n] = vset->next;

                    vnam = read_ARDF_VNAM(&b, start, size, -1, error);
                    if (vnam == NULL) {
                        //err_INVALID(error, "VOLM VNAM");
                        goto fail;
                    }

                    for (c = 0; c < numb_channels; c++) {
                        volm_dfield = gwy_brick_get_data(volm_channels[c]);
                        vdat = read_ARDF_VDAT(&b, start, size, -1, error);
                        if (vdat == NULL) {
                            //err_INVALID(error, "VOLM VDAT");
                            goto fail;
                        }
                        //gwy_debug("data size: %i\n", vdat->size_data)
                        if (snake_like) {
                            if (r % 2 == 0)
                                for (j = 0; j < vdat->size_data; j++)
                                    volm_dfield[j*nplane + n + r*numb_points] = vdat->data[j];
                            else
                                for (j = 0; j < vdat->size_data; j++)
                                    volm_dfield[j*nplane + numb_points - n - 1 + r*numb_points] = vdat->data[j];
                        }
                        else {
                            for (j = 0; j < vdat->size_data; j++)
                                volm_dfield[j*nplane + n + r*numb_points] = vdat->data[j];
                        }

                        free_ARDF_VDAT(vdat);
                        vdat = NULL;
                        //g_object_unref(volm_dfield); //causes segfaults
                    }

                    if (!read_XDAT(&b, start, size, -1, error)) {
                        //err_INVALID(error, "VOLM XDAT");
                        goto fail;
                    }

                    GWY_FREE(vset);
                    free_ARDF_VNAM(vnam);
                    vnam = NULL;
                }

                g_free(numb_force);
                g_free(numb_line);
                g_free(numb_point);
                g_free(loc_prev);
                g_free(loc_next);
            }

            if (!gwy_app_wait_set_fraction(i*numb_volm + r*1.0/(numb_lines*numb_volm))) {
                err_INVALID(error, "set load percentage");
                goto fail;
            }
        }

        for (j = 0; j < numb_channels; j++) {
            gwy_brick_invert(volm_channels[j], FALSE, FALSE, FALSE, TRUE);
            gwy_container_pass_object(container, gwy_app_get_brick_key_for_id(c_volm), volm_channels[j]);
            gwy_container_set_const_string(container, gwy_app_get_brick_title_key_for_id(c_volm), vchn[j]);
            gwy_file_volume_import_log_add(container, j, NULL, filename);
            c_volm ++;
        }

        //g_object_unref(dfield); //causes errors
        //g_object_unref(gbrick);

        GWY_FREE(volm_channels);
        GWY_FREE(vdef);
        for (j = 0; j < c_vchn; j++) {
            g_free(vchn[j]);
        }
        GWY_FREE(vchn);
        //g_free(xdef_text);
        free_ARDF_TOC(volm);
        volm = NULL;
        free_ARDF_TOC(volm_ttoc);
        volm_ttoc = NULL;
        free_ARDF_TOC(idx);
        idx = NULL;
    }

fail:  //FIXME: some fails lead to free() error
    if (wait_dialogue_shown)
        gwy_app_wait_finish();
    g_free(apoint);
    GWY_OBJECT_UNREF(instrument_data);
    free_ARDF_TOC(ftoc);
    free_ARDF_TOC(ttoc);
    free_ARDF_TOC(imtoc);
    free_ARDF_TOC(imttoc);
    free_ARDF_TOC(idat);
    free_ARDF_TOC(volm);
    free_ARDF_TOC(volm_ttoc);
    free_ARDF_TOC(idx);
    GWY_OBJECT_UNREF(meta);
    g_free(imdef);
    g_free(vdef);
    if (channel_name != NULL)
        g_string_free(channel_name, TRUE);
    g_free(volm_channel);
    //if (unit != NULL) g_object_unref(unit);
    //if (dfield != NULL) g_object_unref(dfield);
    if (volm_channels != NULL) {
        for (i = 0; i < numb_channels; i++) {
            g_free(volm_channels[i]);
        }
        g_free(volm_channels);
    }
    if (vchn != NULL) {
        for (i = 0; i < c_vchn; i++) {
            g_free(vchn[i]);
        }
        g_free(vchn);
    }
    g_free(vset);
    free_ARDF_VNAM(vnam);
    free_ARDF_VDAT(vdat);
    free_ARDF_TEXT(txt);
    free_ARDF_TEXT(raw_note);
    gwy_file_abandon_contents(buffer, size, NULL);
    return container;
}

static inline void
err_HEADER_MISMATCH(GError **error, const gchar *found, const gchar *expected)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                _("Found data header %s instead of expected %s."),
                found, expected);
}

static ARDFPointer*
ardf_read_pointer(guchar **pp, guchar *start, gsize size, gint32 address, GError **error)
{
    ARDFPointer *pointer;
    const guchar *p;

    if (address != -1) {
        if ((address < -1) || (address > size)) {
            err_TRUNCATED_HEADER(error); //errors should be reported
            return NULL;         // either here or in functions using
        }                        // this, but not both
        *pp = start + address;
    }

    if (*pp - start + 16 > size) {
        printf("buffer overflow");
        err_TRUNCATED_HEADER(error);
        return NULL;
    }

    pointer = g_new(ARDFPointer, 1);

    p = *pp;

    pointer->check_CRC32 = gwy_get_guint32_le(&p);
    pointer->size_bytes = gwy_get_guint32_le(&p);
    get_CHARARRAY(pointer->type_pnt, &p);
    pointer->misc_num = gwy_get_guint32_le(&p);

    *pp += 16;

    return pointer; //dealoc with g_free
}

static gboolean
check_type(guchar expected[4], guchar found[4], GError **error)
{
    if (memcmp(found, expected, 4) != 0) {
        //printf("wrong header - found %s expected %s\n", found, expected);
        err_HEADER_MISMATCH(error, found, expected);
        return FALSE;
    }
    return TRUE;
    //else gwy_debug("check passed\n");
}

static ARDF_TOC*
read_ARDF_TOC(guchar **pp, guchar *start, gsize size, gint32 address, guchar pointer_type[4], GError **error)
{
    ARDF_TOC *toc;
    ARDFPointer *apoint, *bpoint;
    const guchar *p;
    guchar *b; // b and p are used same, redundancy is for function compatibility
    guint32 c_imag, c_volm, c_next, c_nset, c_thmb, c_toff, c_idat, c_voff;
    gboolean done;
    guint32 numb_read, i;
    guint64 last_pointer, last_index, last_lin_point;
    guint32 last_pnt_count, last_lin_count;
    guint32 size_read;
    guint64 *pnt_imag = NULL;
    guint64 *pnt_volm = NULL;
    guint64 *pnt_next = NULL;
    guint64 *pnt_nset = NULL;
    guint64 *pnt_thmb = NULL;
    guint64 *idx_text = NULL;
    guint64 *pnt_text = NULL;
    guint64 *lin_pointer = NULL;
    guint32 *pnt_counter = NULL;
    guint32 *lin_counter = NULL;
    gfloat *data = NULL;
    gfloat *last_data = NULL;
    guint32 j;

    if (address != -1) {
        if ((address < -1) || (address > size)) {
            err_TRUNCATED_HEADER(error);
            return NULL;
        }
        *pp = start + address;
    }

    toc = g_new(ARDF_TOC, 1);

    apoint = ardf_read_pointer(pp, start, size, -1, error);
    if (apoint == NULL) {
        g_free(toc);
        g_free(apoint);
        return NULL;
    }
    if (!check_type(pointer_type, apoint->type_pnt, error)) {
        g_free(toc);
        g_free(apoint);
        return NULL;
    }

    if (*pp - start + 16 > size) {
        err_TRUNCATED_HEADER(error);
        g_free(toc);
        g_free(apoint);
        return NULL;
        //printf("buffer overflow");
    }
    p = *pp;
    toc->size_table = gwy_get_guint64_le(&p);
    toc->numb_entry = gwy_get_guint32_le(&p);
    toc->size_entry = gwy_get_guint32_le(&p);
    *pp += 16;

    //precompute needed array sizes
    c_imag = 0;
    c_volm = 0;
    c_next = 0;
    c_nset = 0;
    c_thmb = 0;
    c_toff = 0;
    c_idat = 0;
    c_voff = 0;
    done = FALSE;
    numb_read = 1;
    b = *pp;
    while (!done && numb_read <= toc->numb_entry) {
        gwy_debug("*");
        j += 1;
        bpoint = ardf_read_pointer(&b, start, size, -1, error);
        if (!bpoint) {
            g_free(toc);
            g_free(apoint);
            return NULL;
        }
        if (b - start + toc->size_entry - 16 > size) {
            err_TRUNCATED_HEADER(error);
            g_free(toc);
            g_free(apoint);
            g_free(bpoint);
            return NULL;
            //printf("buffer overflow");
        }
        if (toc->size_entry == 24)
            b += 8;
        else if (toc->size_entry == 32)
            b += 16;
        else if (toc->size_entry == 40)
            b += 24;
        else
            b += (toc->size_entry - 16);
        if (memcmp(TYPE_IMAG, bpoint->type_pnt, 4) == 0) {
            c_imag += 1;
        }
        else if (memcmp(TYPE_VOLM, bpoint->type_pnt, 4) == 0) {
            c_volm += 1;
        }
        else if (memcmp(TYPE_NEXT, bpoint->type_pnt, 4) == 0) {
            c_next += 1;
        }
        else if (memcmp(TYPE_NSET, bpoint->type_pnt, 4) == 0) {
            c_nset += 1;
        }
        else if (memcmp(TYPE_THMB, bpoint->type_pnt, 4) == 0) {
            c_thmb += 1;
        }
        else if (memcmp(TYPE_TOFF, bpoint->type_pnt, 4) == 0) {
            c_toff += 1;
        }
        else if (memcmp(TYPE_IDAT, bpoint->type_pnt, 4) == 0) {
            c_idat += 1;
        }
        else if (memcmp(TYPE_VOFF, bpoint->type_pnt, 4) == 0) {
            c_voff += 1;
        }
        else if (memcmp(TYPE_XXXX, bpoint->type_pnt, 4) == 0) {
            if (memcmp(TYPE_IBOX, apoint->type_pnt, 4) == 0) {
                c_idat += 1;  //uses same list as IDAT
                gwy_debug("|");
            }
            else if (memcmp(TYPE_VTOC, apoint->type_pnt, 4) == 0) {
                c_voff += 1; //uses same lists as VOFF
            }
            else
                done = TRUE;
        }
        else {
            err_HEADER_MISMATCH(error, bpoint->type_pnt, "IMAG/VOLM/NEXT/NSET/THMB/TOFF/IDAT/VOFF/IBOX/VTOC");
            g_free(toc);
            g_free(apoint);
            g_free(bpoint);
            return NULL;
            //printf("%s header not recognized", bpoint->type_pnt);
        }
        numb_read += 1;
        g_free(bpoint);
    }
    gwy_debug("done\n");

    //initialize data fields
    size_read = 0;
    bpoint = ardf_read_pointer(&b, start, size, -1, error);
    if (memcmp(TYPE_IDAT, bpoint->type_pnt, 4) == 0) {
        size_read = (toc->size_entry - 16)/4;
        data = g_new(gfloat, c_idat*size_read);
        gwy_debug("idat count: %i\n", c_idat);
        last_data = g_new(gfloat, size_read);

        toc->x_size = c_idat;
        toc->y_size = size_read;
    }
    else if (toc->size_entry == 24) {
        pnt_imag = g_new(guint64, c_imag);
        pnt_volm = g_new(guint64, c_volm);
        pnt_next = g_new(guint64, c_next);
        pnt_nset = g_new(guint64, c_nset);
        pnt_thmb = g_new(guint64, c_thmb);
    }
    else if (toc->size_entry == 32) {
        idx_text = g_new(guint64, c_toff);
        pnt_text = g_new(guint64, c_toff);
    }
    else if (toc->size_entry == 40) {
        pnt_counter = g_new(guint32, c_voff);
        lin_counter = g_new(guint32, c_voff);
        lin_pointer = g_new(guint64, c_voff);
    }
    else {
        size_read = (toc->size_entry - 16)/4;
        data = g_new(gfloat, c_idat*size_read);
        gwy_debug("idat count: %i\n", c_idat);
        gwy_debug("idat found with a backup else, data field init probably doesn't work");
        last_data = g_new(gfloat, size_read);

        toc->x_size = c_idat;
        toc->y_size = size_read;
    }
    toc->pnt_imag = pnt_imag;
    toc->len_imag = c_imag;
    toc->pnt_volm = pnt_volm;
    toc->len_volm = c_volm;
    toc->pnt_next = pnt_next;
    toc->pnt_nset = pnt_nset;
    toc->pnt_thmb = pnt_thmb;
    toc->idx_text = idx_text;
    toc->pnt_text = pnt_text;
    toc->len_text = c_toff;
    toc->pnt_counter = pnt_counter;
    toc->lin_counter = lin_counter;
    toc->lin_pointer = lin_pointer;
    toc->data = data;
    toc->len_data = c_idat;
    g_free(bpoint);

    c_imag = 0;
    c_volm = 0;
    c_next = 0;
    c_nset = 0;
    c_thmb = 0;
    c_toff = 0;
    c_voff = 0;
    c_idat = 0;

    //load data
    done = FALSE;
    numb_read = 1;
    last_pointer = 0; // 0 should be NULL, but it's wrong data type
    last_index = 0;
    last_pnt_count = 0;
    last_lin_count = 0;
    last_lin_point = 0;
    while (!done && numb_read <= toc->numb_entry) {
        bpoint = ardf_read_pointer(pp, start, size, -1, error);
        if (bpoint == NULL) {
            g_free(toc);
            g_free(apoint);
            g_free(last_data);
            return NULL;
        }
        p = *pp;

        if (memcmp(TYPE_IDAT, bpoint->type_pnt, 4) == 0) {
            for (i = 0; i < size_read; i++) {
                last_data[i] = gwy_get_gfloat_le(&p);
                *pp += 4;
            }
	}
	else if (toc->size_entry == 24) {
            last_pointer = gwy_get_guint64_le(&p);
            *pp += 8;
        }
        else if (toc->size_entry == 32) {
            last_index = gwy_get_guint64_le(&p);
            last_pointer = gwy_get_guint64_le(&p);
            *pp += 16;
        }
        else if (toc->size_entry == 40) {
            last_pnt_count = gwy_get_guint32_le(&p);
            last_lin_count = gwy_get_guint32_le(&p);
            p += 8;
            last_lin_point = gwy_get_guint64_le(&p);
            *pp += 24;
        }
        else {
            for (i = 0; i < size_read; i++) {
                last_data[i] = gwy_get_gfloat_le(&p);
                *pp += 4;
            }
        }

        if (memcmp(TYPE_IMAG, bpoint->type_pnt, 4) == 0) {
            pnt_imag[c_imag] = last_pointer;
            c_imag += 1;
        }
        else if (memcmp(TYPE_VOLM, bpoint->type_pnt, 4) == 0) {
            pnt_volm[c_volm] = last_pointer;
            c_volm += 1;
        }
        else if (memcmp(TYPE_NEXT, bpoint->type_pnt, 4) == 0) {
            pnt_next[c_next] = last_pointer;
            c_next += 1;
        }
        else if (memcmp(TYPE_NSET, bpoint->type_pnt, 4) == 0) {
            pnt_nset[c_nset] = last_pointer;
            c_nset += 1;
        }
        else if (memcmp(TYPE_THMB, bpoint->type_pnt, 4) == 0) {
            pnt_thmb[c_thmb] = last_pointer;
            c_thmb += 1;
        }
        else if (memcmp(TYPE_TOFF, bpoint->type_pnt, 4) == 0) {
            idx_text[c_toff] = last_index;
            pnt_text[c_toff] = last_pointer;
            c_toff += 1;
        }
        else if (memcmp(TYPE_IDAT, bpoint->type_pnt, 4) == 0) {
            for (i = 0; i < size_read; i++) {
                data[c_idat*size_read + i] = last_data[i];
            }
            c_idat += 1;
        }
        else if (memcmp(TYPE_VOFF, bpoint->type_pnt, 4) == 0) {
            pnt_counter[c_voff] = last_pnt_count;
            lin_counter[c_voff] = last_lin_count;
            lin_pointer[c_voff] = last_lin_point;
            c_voff += 1;
        }
        else if (memcmp(TYPE_XXXX, bpoint->type_pnt, 4) == 0) {
            if (memcmp(TYPE_IBOX, apoint->type_pnt, 4) == 0) {
                for (i = 0; i < size_read; i++) {
                    data[c_idat*size_read + i] = last_data[i];
                }
                c_idat += 1;
            }
           else if (memcmp(TYPE_VTOC, apoint->type_pnt, 4) == 0) {
                pnt_counter[c_voff] = last_pnt_count;
                lin_counter[c_voff] = last_lin_count;
                lin_pointer[c_voff] = last_lin_point;
                c_voff += 1;
           }
           else
               done = TRUE;
        }
        else {
            err_HEADER_MISMATCH(error, bpoint->type_pnt, "IMAG/VOLM/NEXT/NSET/THMB/TOFF/IDAT/VOFF/IBOX/VTOC");
            g_free(toc);
            g_free(apoint);
            g_free(bpoint);
            g_free(last_data);
            return NULL;
            //printf("%s not recognized!\n", bpoint->type_pnt);
        }

        numb_read += 1;
        g_free(bpoint);
    }

    g_free(apoint);
    g_free(last_data);
    // use free_ARDF_TOC to dealoc toc
    return toc;
}

static ARDF_VSET*
read_ARDF_VSET(guchar **pp, guchar *start, gsize size, gint32 address, GError **error)
{
    ARDFPointer *apoint;
    ARDF_VSET *vset;
    const guchar *p;

    vset = g_new(ARDF_VSET, 1);

    if (address != -1) {
        if ((address < -1) || (address > size)) {
            err_TRUNCATED_HEADER(error);
            g_free(vset);
            return NULL;
        }
        *pp = start + address;
    }

    apoint = ardf_read_pointer(pp, start, size, -1, error);
    if (apoint == NULL) {
        g_free(vset);
        return NULL;
    }
    if (!check_type(TYPE_VSET, apoint->type_pnt, error)) {
        g_free(vset);
        g_free(apoint);
        return NULL;
    }

    if (*pp - start + 36 > size) {
        printf("buffer overflow");
        err_TRUNCATED_HEADER(error);
        g_free(vset);
        g_free(apoint);
        return NULL;
    }
    p = *pp;
    vset->force = gwy_get_guint32_le(&p);
    vset->line = gwy_get_guint32_le(&p);
    vset->point = gwy_get_guint32_le(&p);
    p += 4;
    vset->prev = gwy_get_guint64_le(&p);
    vset->next = gwy_get_guint64_le(&p);
    *pp += 32;

    g_free(apoint);
    return vset; //dealoc after use
}

static ARDF_DEF*
read_ARDF_DEF(guchar **pp, guchar *start, gsize size, gint32 address, guchar pointer_type[4], GError **error)
{
    ARDFPointer *apoint;
    ARDF_DEF *def;
    const guchar *p;
    guint32 skip_len;

    def = g_new(ARDF_DEF, 1);

    if (address != -1) {
        if ((address < -1) || (address > size)) {
            err_TRUNCATED_HEADER(error);
            g_free(def);
            return NULL;
        }
        *pp = start + address;
    }

    apoint = ardf_read_pointer(pp, start, size, -1, error);
    if (apoint == NULL) {
        g_free(def);
        return NULL;
    }
    if (!check_type(pointer_type, apoint->type_pnt, error)) {
        g_free(def);
        g_free(apoint);
        return NULL;
    }

    if (memcmp(TYPE_IDEF, apoint->type_pnt, 4) == 0) {
        skip_len = 96;
    }
    if (memcmp(TYPE_VDEF, apoint->type_pnt, 4) == 0) {
        skip_len = 144;
    }

    if (*pp - start + apoint->size_bytes > size) {
        printf("buffer overflow");
        err_TRUNCATED_HEADER(error);
        g_free(def);
        g_free(apoint);
        return NULL;
    }
    p = *pp;
    def->points = gwy_get_guint32_le(&p);
    def->lines = gwy_get_guint32_le(&p);
    p += skip_len;
    get_CHARARRAY(def->image_title, &p);
    *pp += apoint->size_bytes - 16;

    g_free(apoint);
    return def; //dealoc after use
}

static ARDF_TEXT*
read_ARDF_TEXT(guchar **pp, guchar *start, gsize size, gint32 address, GError **error)
{
    ARDFPointer *apoint;
    ARDF_TEXT *txt = NULL;
    const guchar *p;
    guint64 size_note;

    txt = g_new(ARDF_TEXT, 1);

    if (address != -1) {
        if ((address < -1) || (address > size)) {
            err_TRUNCATED_HEADER(error);
            g_free(txt);
            return NULL;
        }
        *pp = start + address;
    }

    apoint = ardf_read_pointer(pp, start, size, -1, error);
    if (apoint == NULL) {
        g_free(txt);
        return NULL;
    }
    if (!check_type(TYPE_TEXT, apoint->type_pnt, error)) {
        g_free(txt);
        g_free(apoint);
        return NULL;
    }

    if (*pp - start + 8 > size) {
        printf("buffer overflow");
        err_TRUNCATED_HEADER(error);
        g_free(txt);
        g_free(apoint);
        return NULL;
    }
    *pp += 4;
    p = *pp;
    size_note = gwy_get_guint32_le(&p);
    *pp += 4;

    //gwy_debug("text size: %li\n", size_note);

    if (*pp - start + size_note > size) {
        printf("buffer overflow");
        err_TRUNCATED_HEADER(error);
        g_free(txt);
        g_free(apoint);
        return NULL;
    }
    txt->text = g_new(guchar, size_note + 1);
    p = *pp;
    get_CHARS(txt->text, &p, size_note);
    // lines are separated with \r, which causes confusion in debug prints
    txt->text[size_note] = '\0';
    txt->len = size_note;

    g_free(apoint);
    return txt;  //remember to dealoc with free_ARDF_TEXT
}

static ARDF_VNAM*
read_ARDF_VNAM(guchar **pp, guchar *start, gsize size, gint32 address, GError **error)
{
    ARDFPointer *apoint;
    ARDF_VNAM *vnam;
    const guchar *p;

    vnam = g_new(ARDF_VNAM, 1);

    if (address != -1) {
        if ((address < -1) || (address > size)) {
            err_TRUNCATED_HEADER(error);
            g_free(vnam);
            return NULL;
        }
        *pp = start + address;
    }

    apoint = ardf_read_pointer(pp, start, size, -1, error);
    if (apoint == NULL) {
        g_free(vnam);
        return NULL;
    }
    if (!check_type(TYPE_VNAM, apoint->type_pnt, error)) {
        g_free(vnam);
        g_free(apoint);
        return NULL;
    }

    if (*pp - start + 16 > size) {
        //printf("buffer overflow");
        err_TRUNCATED_HEADER(error);
        g_free(vnam);
        g_free(apoint);
        return NULL;
    }
    p = *pp;
    vnam->force = gwy_get_guint32_le(&p);
    vnam->line = gwy_get_guint32_le(&p);
    vnam->point = gwy_get_guint32_le(&p);
    vnam->size_text = gwy_get_guint32_le(&p);
    *pp += 16;

    if (*pp - start + apoint->size_bytes - 32 > size) {
        printf("buffer overflow");
        err_TRUNCATED_HEADER(error);
        g_free(vnam);
        g_free(apoint);
        return NULL;
    }
    p = *pp;
    vnam->name = g_new(guchar, vnam->size_text);
    get_CHARARRAY(vnam->name, &p);
    *pp += apoint->size_bytes - 32;

    g_free(apoint);
    return vnam; //should be also dealoced
}

static ARDF_VDAT*
read_ARDF_VDAT(guchar **pp, guchar *start, gsize size, gint32 address, GError **error)
{
    ARDFPointer *apoint;
    ARDF_VDAT *vdat;
    const guchar *p;
    guint32 i;

    vdat = g_new(ARDF_VDAT, 1);

    if (address != -1) {
        if ((address < -1) || (address > size)) {
            err_TRUNCATED_HEADER(error);
            g_free(vdat);
            return NULL;
        }
        *pp = start + address;
    }

    apoint = ardf_read_pointer(pp, start, size, -1, error);
    if (apoint == NULL) {
        g_free(vdat);
        return NULL;
    }
    if (!check_type(TYPE_VDAT, apoint->type_pnt, error)) {
        g_free(vdat);
        g_free(apoint);
        return NULL;
    }

    if (*pp - start + apoint->size_bytes - 16 > size) {
        printf("buffer overflow");
        err_TRUNCATED_HEADER(error);
        g_free(vdat);
        g_free(apoint);
        return NULL;
    }
    p = *pp;
    vdat->force = gwy_get_guint32_le(&p);
    vdat->line = gwy_get_guint32_le(&p);
    vdat->point = gwy_get_guint32_le(&p);
    vdat->size_data = gwy_get_guint32_le(&p);
    vdat->force_type = gwy_get_guint32_le(&p);
    vdat->pnt0 = gwy_get_guint32_le(&p);
    vdat->pnt1 = gwy_get_guint32_le(&p);
    vdat->pnt2 = gwy_get_guint32_le(&p);
    p += 8;

    vdat->data = g_new(gfloat, vdat->size_data);
    for (i = 0; i < vdat->size_data; i++) {
        vdat->data[i] = gwy_get_gfloat_le(&p);
    }
    *pp += apoint->size_bytes - 16;

    g_free(apoint);
    return vdat; //must dealoc later using free_ARDF_VDAT
}

static gboolean
read_XDAT(guchar **pp, guchar *start, gsize size, gint32 address, GError **error)
{
    ARDFPointer *apoint;

    if (address != -1) {
        if ((address < -1) || (address > size)) {
            err_TRUNCATED_HEADER(error);
            return FALSE;
        }
        *pp = start + address;
    }

    apoint = ardf_read_pointer(pp, start, size, -1, error);
    if (apoint == NULL)
        return FALSE;
    if (memcmp(TYPE_XDAT, apoint->type_pnt, 4)
        && memcmp(TYPE_VSET, apoint->type_pnt, 4)
        && memcmp(TYPE_THMB, apoint->type_pnt, 4)) {
        //printf("expected XDAT, VSET or THMB, found %s\n", apoint->type_pnt);
        err_HEADER_MISMATCH(error, apoint->type_pnt, "XDAT/VSET/THMB");
        g_free(apoint);
        return FALSE;
    }

    if (memcmp(TYPE_XDAT, apoint->type_pnt, 4) == 0)
        *pp += apoint->size_bytes - 16;
    if (memcmp(TYPE_VSET, apoint->type_pnt, 4) == 0)
        *pp -= 16;
    if (memcmp(TYPE_THMB, apoint->type_pnt, 4) == 0) {
        gwy_debug("read THMB in volm data, may be unexpected\n");
    }

    g_free(apoint);
    return TRUE;
}

static GwyContainer*
parse_metadata(ARDF_TEXT *raw)
{
    GwyContainer *meta = NULL;
    guchar keyword_stack[raw->len];
    guchar value_stack[raw->len];
    guchar* key;
    //guchar last_char = 'a';
    gboolean value_wanted = FALSE;
    guint32 i, key_i, val_i;

    meta = gwy_container_new();

    key_i = 0;
    val_i = 0;

    for (i = 0; i < raw->len; i++) {
        if (raw->text[i] == ':') {
            if (!value_wanted)
                value_wanted = TRUE;
            else {
                value_stack[val_i] = '\\';
                val_i += 1;
            }
        }
        else if (((guint32)raw->text[i] >= 32) && ((guint32)raw->text[i] < 187)) {
            if (!value_wanted) {
                keyword_stack[key_i] = raw->text[i];
                key_i += 1;
            }
            else {
                if (!((val_i == 0) && (raw->text[i] == ' '))) {
                    //ignore left trailing whitespace
                    value_stack[val_i] = raw->text[i];
                    val_i += 1;
                }
            }
        }
        else if (raw->text[i] == '\r') {
            if (value_wanted) {
                //put value-key pair
                key = g_strndup(keyword_stack, key_i);
                gwy_container_set_string_by_name(meta, key, g_strndup(value_stack, val_i));
                key_i = 0;
                val_i = 0;
                value_wanted = FALSE;
                GWY_FREE(key);
            }
        }

        //last_char = raw->text[i];
    }

    return meta; //remember to dealoc after use
}

static void
free_ARDF_TOC(ARDF_TOC *toc)
{
    if (toc != NULL) {
        g_free(toc->pnt_imag);
        g_free(toc->pnt_volm);
        g_free(toc->pnt_next);
        g_free(toc->pnt_nset);
        g_free(toc->pnt_thmb);
        g_free(toc->idx_text);
        g_free(toc->pnt_text);
        g_free(toc->lin_pointer);
        g_free(toc->pnt_counter);
        g_free(toc->lin_counter);
        g_free(toc->data);
        g_free(toc);
    }
}

static void
free_ARDF_TEXT(ARDF_TEXT *txt)
{
    if (txt != NULL) {
        g_free(txt->text);
        g_free(txt);
    }
}

static void
free_ARDF_VNAM(ARDF_VNAM *vnam)
{
    if (vnam != NULL) {
         g_free(vnam->name);
         g_free(vnam);
    }
}

static void
free_ARDF_VDAT(ARDF_VDAT *vdat)
{
    if (vdat != NULL) {
        g_free(vdat->data);
        g_free(vdat);
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
