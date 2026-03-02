/*
 *  $Id: gwyzlib.h 25989 2023-10-31 17:27:00Z yeti-dn $
 *  Copyright (C) 2015-2023 David Necas (Yeti).
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
#ifndef __GWY_FILE_ZLIB_H__
#define __GWY_FILE_ZLIB_H__

#include <string.h>
#include <glib.h>

/* FIXME: Use GZlibDecompressor? It may actually more complicated to wrap than zlib itself because GConverter has a
 * *complicated* state management. */

/* This is the function we always define. It will just fail if zlib is not available. Check HAVE_ZLIB if zlib
 * compression is a mandatory part of the module.
 *
 * Pass NULL as uncbuffer if you want the function to allocate a new buffer. If data size is unknown (zero uncsize)
 * you must pass NULL uncbuffer. The function cannot decompress zero amount of data – why would you need to do that?
 *
 * Upon success, csize is set to the actual uncompressed data size, which for known data size means it should be
 * unchanged. And csize is set to the number of unconsumed bytes in cbuffer.
 *
 * Note however, that for fixed size uncompressed data success means we could produce *at least* this amount of data.
 * It allows reading just the beginning of a stream, but you need to check csize if you want to be sure all input
 * bytes were consumed. */
static guchar* gwyzlib_unpack_compressed_data(const guchar *cbuffer,
                                              gsize *csize,
                                              guchar *uncbuffer,
                                              gsize *uncsize,
                                              GError **error);

/****************************************************************************
 *
 * Simple zlib decompression wrapper
 *
 ****************************************************************************/

#ifdef HAVE_ZLIB
#include <zlib.h>

G_GNUC_UNUSED
static guchar*
gwyzlib_unpack_compressed_data(const guchar *cbuffer,
                               gsize *csize,
                               guchar *uncbuffer,
                               gsize *uncsize,
                               GError **error)
{
    z_stream zbuf; /* decompression stream */
    gboolean output_size_known = !!*uncsize, allocated_buffer = FALSE;
    gsize usize = output_size_known ? *uncsize : 16;
    guchar *retval = NULL;
    gsize prev_total_in = 0, prev_total_out = 0;
    gint status, no_progres;

    g_return_val_if_fail(output_size_known || !uncbuffer, NULL);

    gwy_clear(&zbuf, 1);
    zbuf.next_in = (char*)cbuffer;
    zbuf.avail_in = *csize;
    zbuf.next_out = uncbuffer;
    zbuf.avail_out = usize;

    /* XXX: zbuf->msg does not seem to ever contain anything, so just report the error codes. */
    if ((status = inflateInit(&zbuf)) != Z_OK) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("zlib initialization failed with error %d, cannot decompress data."), status);
        return NULL;
    }

    if (!uncbuffer) {
        zbuf.next_out = uncbuffer = g_new(guchar, usize);
        allocated_buffer = TRUE;
    }

    /* Z_STREAM_END: we exhausted all input. */
    no_progres = 0;
    while ((status == inflate(&zbuf, Z_FINISH)) != Z_STREAM_END) {
        /* Z_OK: decompression succeeded, but can continue. If output size is known we handle it later. */
        if (status == Z_OK || status == Z_BUF_ERROR) {
            /* Does zlib keeps returning Z_OK but not progress?
             *
             * Unfortunately this can happen. The library keeps waiting for us to give it more input and we keep
             * waiting for it to either do something or report an error. Force termination of the possible infinite
             * loop, one way or another. */
            if (status == Z_OK && !zbuf.avail_in)
                break;
            if (status == Z_OK && zbuf.total_in == prev_total_in && zbuf.total_out == prev_total_out) {
                if (++no_progres == 3)
                    break;
            }
            else
                no_progres = 0;

            if (output_size_known) {
                if (zbuf.total_out == usize)
                    break;
                /* Otherwise the above check should prevent a stupid infinite loop. */
            }
            else {
                /* Do not double the buffer if zlib is just being silly and there is clearly some space. */
                if (zbuf.avail_out < 16) {
                    uncbuffer = g_realloc(uncbuffer, 2*usize);
                    zbuf.next_out = uncbuffer + usize;
                    zbuf.avail_out = usize;
                    usize *= 2;
                }
            }
        }
        else {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Decompression of compressed data failed with error %d."), status);
            goto end;
        }
        prev_total_in = zbuf.total_in;
        prev_total_out = zbuf.total_out;
    }

    if (output_size_known) {
        /* Allow decompressing less data than available. Reaching the end is not an error. */
        if (zbuf.total_out == usize)
            retval = uncbuffer;
        else {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Decompressed data size (%lu bytes) does not match expected size (%lu bytes)."),
                        (gulong)zbuf.total_out, (gulong)usize);
        }
    }
    else {
        /* FIXME: if zbuf.total_out is zero this should be an error. */
        retval = uncbuffer = g_realloc(uncbuffer, zbuf.total_out);
        *uncsize = zbuf.total_out;
    }

end:
    /* Report the amount of remaining input. This is how the caller can see if all input has been consumed. */
    *csize -= zbuf.total_in;

    status = inflateEnd(&zbuf);
    /* This should not really happen whatever data we pass in.  And we either have already our output or already have
     * some other error, so just make some noise and get over it.  */
    if (status != Z_OK)
        g_critical("inflateEnd() failed with error %d", status);

    if (!retval && allocated_buffer)
        g_free(uncbuffer);

    return retval;
}
#else
G_GNUC_UNUSED
static guchar*
gwyzlib_unpack_compressed_data(G_GNUC_UNUSED const guchar *cbuffer,
                               G_GNUC_UNUSED gsize *csize,
                               G_GNUC_UNUSED guchar *uncbuffer,
                               G_GNUC_UNUSED gsize *uncsize,
                               GError **error)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                _("Cannot decompress compressed data.  Gwyddion was built without %s support."), "zlib");
    return NULL;
}
#endif

#endif

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
