/*
 * $Id: gwymd5.c 25968 2023-10-27 14:46:54Z yeti-dn $
 *
 * This code implements the MD5 message-digest algorithm.
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 *
 * GIMPified 2002 by Sven Neumann <sven@gimp.org>
 * Gwyddionized 2004 by Yeti <yeti@physics.muni.cz>
 */

/* parts of this file are :
 * Written March 1993 by Branko Lankester
 * Modified June 1993 by Colin Plumb for altered md5.c.
 * Modified October 1995 by Erik Troan for RPM
 */

#include "config.h"
#include <string.h>
#include <glib.h>
#include "gwymd5.h"

/**
 * gwy_md5_get_digest:
 * @buffer: A byte buffer.
 * @buffer_size: Size of @buffer (in bytes) or -1 if @buffer is nul-terminated.
 * @digest: 16 bytes to store the hash code to.
 *
 * Compute the MD5 hash of a buffer.
 *
 * The MD5 algorithm takes as input a message of arbitrary length and produces as output a 128-bit "fingerprint" or
 * "message digest" of the input.  For more information see RFC 1321.
 *
 * This function is trivially replaced by #GChecksum with %G_CHECKSUM_MD5. It is in fact currently a simple #GChecksum
 * wrapper.
 **/
void
gwy_md5_get_digest(const gchar *buffer,
                   gint buffer_size,
                   guchar digest[16])
{
    GChecksum *checksum;
    gsize digest_len = 16;

    g_return_if_fail(buffer);
    g_return_if_fail(digest);

    if (buffer_size < 0)
        buffer_size = strlen(buffer);

    checksum = g_checksum_new(G_CHECKSUM_MD5);
    g_checksum_update(checksum, buffer, buffer_size);
    g_checksum_get_digest(checksum, digest, &digest_len);
    g_assert(digest_len == 16);
    g_checksum_free(checksum);
}

/************************** Documentation ****************************/

/**
 * SECTION:gwymd5
 * @title: gwymd5
 * @short_description: Compute MD5 digest
 *
 * MD5 (<ulink url="http://www.faqs.org/rfcs/rfc1321.html">RFC1321</ulink>) is a reasonably fast digest function.  It
 * can be used for hash key creation and fast data indexing, but should be no longer used for cryptographic and
 * security purposes.
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
