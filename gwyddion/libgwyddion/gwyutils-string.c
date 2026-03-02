/*
 *  $Id: gwyutils-string.c 25679 2023-09-18 16:35:11Z yeti-dn $
 *  Copyright (C) 2003-2023 David Necas (Yeti), Petr Klapetek.
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
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>

#define TOLOWER(c) ((c) >= 'A' && (c) <= 'Z' ? (c) - 'A' + 'a' : (c))

/**
 * gwy_strkill:
 * @s: A NUL-terminated string.
 * @killchars: A string containing characters to kill.
 *
 * Removes characters in @killchars from string @s, modifying it in place.
 *
 * Use gwy_strkill(g_strdup(@s), @killchars) to get a modified copy.
 *
 * Returns: @s itself, the return value is to allow function call nesting.
 **/
gchar*
gwy_strkill(gchar *s,
            const gchar *killchars)
{
    gchar *p, *q;
    gchar killc;

    if (!killchars || !*killchars)
        return s;
    killc = *killchars;
    if (killchars[1])
        g_strdelimit(s, killchars+1, killc);
    if ((p = strchr(s, killc))) {
        for (q = p; *p; p++) {
            if (*p != killc) {
                *q = *p;
                q++;
            }
        }
        *q = '\0';
    }

    return s;
}

/**
 * gwy_strreplace:
 * @haystack: A NUL-terminated string to search in.
 * @needle: A NUL-terminated string to search for.
 * @replacement: A NUL-terminated string to replace @needle with.
 * @maxrepl: Maximum number of occurences to replace (use (gsize)-1 to replace all occurences).
 *
 * Replaces occurences of string @needle in @haystack with @replacement.
 *
 * See gwy_gstring_replace() for a function which does in-place replacement on a #GString.
 *
 * Returns: A newly allocated string.
 **/
gchar*
gwy_strreplace(const gchar *haystack,
               const gchar *needle,
               const gchar *replacement,
               gsize maxrepl)
{
    gsize n, hlen, nlen, rlen, newlen;
    const gchar *p, *pp;
    gchar *dest, *q;

    nlen = strlen(needle);
    g_return_val_if_fail(nlen, NULL);
    n = 0;
    p = haystack;
    while ((p = strstr(p, needle)) && n < maxrepl) {
        p += nlen;
        n++;
    }
    if (!n)
        return g_strdup(haystack);

    hlen = strlen(haystack);
    rlen = strlen(replacement);
    newlen = hlen + n*rlen - n*nlen;

    dest = g_new(gchar, newlen+1);
    pp = haystack;
    q = dest;
    n = 0;
    while ((p = strstr(pp, needle)) && n < maxrepl) {
        memcpy(q, pp, p - pp);
        q += p - pp;
        memcpy(q, replacement, rlen);
        q += rlen;
        pp = p + nlen;
        n++;
    }
    strcpy(q, pp);

    return dest;
}

/**
 * gwy_strdiffpos:
 * @s1: A string.
 * @s2: A string.
 *
 * Finds position where two strings differ.
 *
 * Returns: The last position where the strings do not differ yet.  Possibly -1 can be returned if either string is
 *          %NULL, zero-length, or they differ in the very first character.
 **/
gint
gwy_strdiffpos(const gchar *s1, const gchar *s2)
{
    const gchar *ss = s1;

    if (!s1 || !s2)
        return -1;

    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }

    return (s1 - ss) - 1;
}

/**
 * gwy_strisident:
 * @s: A NUL-terminated string.
 * @more: List of additional ASCII characters allowed inside identifier, empty list can be passed as %NULL.
 * @startmore: List of additional ASCII characters allowed as the first identifier characters, empty list can be
 *             passed as %NULL.
 *
 * Checks whether a string is valid identifier.
 *
 * Valid identifier must start with an alphabetic character or a character from @startmore, and it must continue with
 * alphanumeric characters or characters from @more.
 *
 * Note underscore is not allowed by default, you have to pass it in @more and/or @startmore.
 *
 * Returns: %TRUE if @s is valid identifier, %FALSE otherwise.
 **/
gboolean
gwy_strisident(const gchar *s,
               const gchar *more,
               const gchar *startmore)
{
    const gchar *m;

    g_return_val_if_fail(s, FALSE);
    if (!g_ascii_isalpha(*s)) {
        if (!startmore)
            return FALSE;
        for (m = startmore; *m; m++) {
            if (*s == *m)
                break;
        }
        if (!*m)
            return FALSE;
    }
    s++;

    while (*s) {
        if (!g_ascii_isalnum(*s)) {
            if (!more)
                return FALSE;
            for (m = more; *m; m++) {
                if (*s == *m)
                    break;
            }
            if (!*m)
                return FALSE;
        }
        s++;
    }

    return TRUE;
}

/**
 * gwy_ascii_strcase_equal:
 * @v1: String key.
 * @v2: String key to compare with @v1.
 *
 * Compares two strings for equality, ignoring case.
 *
 * The case folding is performed only on ASCII characters.
 *
 * This function is intended to be passed to g_hash_table_new() as @key_equal_func, namely in conjuction with
 * gwy_ascii_strcase_hash() hashing function.
 *
 * Returns: %TRUE if the two string keys match, ignoring case.
 *
 * Since: 2.26
 */
gboolean
gwy_ascii_strcase_equal(gconstpointer v1,
                        gconstpointer v2)
{
    const gchar *s1 = (const gchar*)v1, *s2 = (const gchar*)v2;

    while (*s1 && *s2) {
        if (TOLOWER(*s1) != TOLOWER(*s2))
            return FALSE;
        s1++, s2++;
    }
    return !*s1 && !*s2;
}

/**
 * gwy_ascii_strcase_hash:
 * @v: String key.
 *
 * Converts a string to a hash value, ignoring case.
 *
 * The case folding is performed only on ASCII characters.
 *
 * This function is intended to be passed to g_hash_table_new() as @hash_func, namely in conjuction with
 * gwy_ascii_strcase_equal() comparison function.
 *
 * Returns: The hash value corresponding to the key @v.
 *
 * Since: 2.26
 */
guint
gwy_ascii_strcase_hash(gconstpointer v)
{
    const signed char *p;
    guint32 h = 5381;

    for (p = v; *p != '\0'; p++)
        h = (h << 5) + h + TOLOWER(*p);

    return h;
}

/**
 * gwy_stramong:
 * @str: A string to find.
 * @...: %NULL-terminated list of string to compare @str with.
 *
 * Checks whether a string is equal to any from given list.
 *
 * Returns: Zero if @str does not equal to any string from the list, nozero othwerise.  More precisely, the position
 *          + 1 of the first string @str equals to is returned in the latter case.
 *
 * Since: 2.11
 **/
guint
gwy_stramong(const gchar *str,
             ...)
{
    va_list ap;
    const gchar *s;
    guint i = 1;

    va_start(ap, str);
    while ((s = va_arg(ap, const gchar*))) {
        if (gwy_strequal(str, s)) {
            va_end(ap);
            return i;
        }
        i++;
    }
    va_end(ap);

    return 0;
}

/**
 * gwy_memmem:
 * @haystack: Memory block to search in.
 * @haystack_len: Size of @haystack, in bytes.
 * @needle: Memory block to find.
 * @needle_len: Size of @needle, in bytes.
 *
 * Find a block of memory in another block of memory.
 *
 * This function is very similar to strstr(), except that it works with arbitrary memory blocks instead of
 * NUL-terminated strings.
 *
 * If @needle_len is zero, @haystack is always returned.
 *
 * On GNU systems with glibc at least 2.1 this is a just a trivial memmem() wrapper.  On other systems it emulates
 * memmem() behaviour but may be a bit slower.
 *
 * Returns: Pointer to the first byte of memory block in @haystack that matches @needle; %NULL if no such block
 *          exists.
 *
 * Since: 2.12
 **/
gpointer
gwy_memmem(gconstpointer haystack,
           gsize haystack_len,
           gconstpointer needle,
           gsize needle_len)
{
#ifdef HAVE_MEMMEM
    return memmem(haystack, haystack_len, needle, needle_len);
#else
    const guchar *h = haystack;
    const guchar *s, *p;
    guchar n0;

    /* Empty needle can be found anywhere */
    if (!needle_len)
        return (gpointer)haystack;

    /* Needle that doesn't fit cannot be found anywhere */
    if (needle_len > haystack_len)
        return NULL;

    /* The general case */
    n0 = *(const guchar*)needle;
    s = h + haystack_len - needle_len;
    for (p = h; p && p <= s; p = memchr(p, n0, s-p + 1)) {
        if (memcmp(p, needle, needle_len) == 0)
            return (gpointer)p;
        p++;
    }
    return NULL;
#endif
}

/**
 * gwy_str_next_line:
 * @buffer: Text buffer.
 *
 * Extracts a next line from a character buffer, modifying it in place.
 *
 * @buffer is updated to point after the end of the line and the "\n" (or "\r" or "\r\n") is replaced with "\0", if
 * present.
 *
 * The final line may or may not be terminated with an EOL marker, its contents is returned in either case.  Note,
 * however, that the empty string "" is not interpreted as an empty unterminated line.  Instead, %NULL is immediately
 * returned.
 *
 * The typical usage of gwy_str_next_line() is:
 * |[
 * gchar *p = text;
 * for (gchar *line = gwy_str_next_line(&p);
 *      line;
 *      line = gwy_str_next_line(&p)) {
 *     g_strstrip(line);
 *     // Do something more with line
 * }
 * ]|
 *
 * Returns: The start of the line.  %NULL if the buffer is empty or %NULL. The return value is
 *          <emphasis>not</emphasis> a new string; the normal return value is the previous value of @buffer.
 **/
gchar*
gwy_str_next_line(gchar **buffer)
{
    gchar *p, *q;

    if (!buffer || !*buffer)
        return NULL;

    q = *buffer;
    if (!*q) {
        *buffer = NULL;
        return NULL;
    }

    for (p = q; *p != '\n' && *p != '\r' && *p; p++)
        ;
    if (p[0] == '\r' && p[1] == '\n') {
        *(p++) = '\0';
        *(p++) = '\0';
    }
    else if (p[0]) {
        *(p++) = '\0';
    }

    *buffer = p;
    return q;
}

/**
 * gwy_str_fixed_font_width:
 * @str: UTF-8 encoded string.
 *
 * Measures the width of UTF-8 encoded string in fixed-width font.
 *
 * This corresponds to width of the string displayed on a text terminal, for instance.  Zero and double width
 * characters are taken into account.  It is not guaranteed all terminals display the string with the calculated
 * width.
 *
 * Returns: String width in fixed font, in character cells.
 *
 * Since: 2.52
 **/
guint
gwy_str_fixed_font_width(const gchar *str)
{
    guint w = 0;
    gunichar u;

    while (str && *str) {
        u = g_utf8_get_char(str);
        if (g_unichar_iswide(u))
            w += 2;
        else if (g_unichar_iszerowidth(u))
            w += 0;
        else
            w += 1;
        str = g_utf8_next_char(str);
    }

    return w;
}

/**
 * gwy_gstring_replace:
 * @str: A #GString string to modify in place.
 * @old: The character sequence to find and replace.  Passing %NULL is the same as passing the empty string.
 * @replacement: The character sequence that should replace @old.  Passing %NULL is the same as passing the empty
 *               string.
 * @count: The maximum number of replacements to make.  A negative number means replacing all occurrences of @old.
 *         Note zero means just zero, i.e. no replacements are made.
 *
 * Replaces non-overlapping occurrences of one string with another in a #GString.
 *
 * Passing %NULL or the empty string for @replacement will cause the occurrences of @old to be removed.
 *
 * Passing %NULL or the empty string for @old means a match occurs at every position in the string, including after
 * the last character.  So @replacement will be inserted at every position in this case.
 *
 * See gwy_strreplace() for a function which creates a new plain C string with substring replacement.
 *
 * Returns: The number of replacements made.  A non-zero value means the string has been modified, no-op replacements
 *          do not count.
 *
 * Since: 2.36
 **/
guint
gwy_gstring_replace(GString *str,
                    const gchar *old,
                    const gchar *replacement,
                    gint count)
{
    guint oldlen, repllen, newlen, ucount, n, i;
    gchar *p, *q, *newp, *newstr;

    g_return_val_if_fail(str, 0);

    if (!old)
        old = "";
    oldlen = strlen(old);

    /* Do we need to do anywork at all? */
    if (!count)
        return 0;
    ucount = (count < 0) ? G_MAXUINT : (guint)count;

    p = str->str;
    if (oldlen && !(p = strstr(str->str, old)))
        return 0;

    if (!replacement)
        replacement = "";
    repllen = strlen(replacement);

    /* Equal lengths, we can do the replacement in place easily. */
    if (oldlen == repllen) {
        if (gwy_strequal(old, replacement))
            return 0;

        n = 0;
        while (p) {
            memcpy(p, replacement, repllen);
            if (++n == ucount)
                break;
            p = strstr(p + oldlen, old);
        }

        return n;
    }

    /* Empty old string: the slightly silly case.  It has a different oldlen semantics so handle it specially. */
    if (!oldlen) {
        gchar *oldcopy;
        guint len;

        ucount = MIN(ucount, str->len + 1);

        if (ucount == 1) {
            g_string_prepend(str, replacement);
            return 1;
        }

        oldcopy = g_strdup(str->str);
        len = str->len;
        g_string_set_size(str, str->len + ucount*repllen);
        g_string_truncate(str, 0);
        p = str->str;
        for (i = 0; i < ucount; i++) {
            memcpy(p, replacement, repllen);
            p += repllen;
            *p = oldcopy[i];
            p++;
        }

        if (ucount < len)
            memcpy(p, oldcopy + ucount, len - ucount);

        g_free(oldcopy);

        return ucount;
    }

    /* The general case.  Count the actual replacement number. */
    n = 0;
    for (q = p; q; q = strstr(q + oldlen, old)) {
        if (++n == ucount)
            break;
    }
    ucount = MIN(n, ucount);

    newlen = str->len;
    if (repllen >= oldlen)
        newlen += ucount*(repllen - oldlen);
    else
        newlen -= ucount*(oldlen - repllen);

    if (!newlen) {
        g_string_truncate(str, 0);
        return ucount;
    }

    /* For just one replacement, do the operation directly. */
    if (ucount == 1) {
        guint pos = p - str->str;

        if (repllen > oldlen) {
            g_string_insert_len(str, pos, replacement, repllen - oldlen);
            memcpy(str->str + pos + (repllen - oldlen), replacement + (repllen - oldlen), oldlen);
        }
        else {
            g_string_erase(str, pos, oldlen - repllen);
            memcpy(str->str + pos, replacement, repllen);
        }
        return 1;
    }

    /* For more replacements, rebuild the string from scratch in a buffer. */
    newstr = g_new(gchar, newlen);

    memcpy(newstr, str->str, p - str->str);
    newp = newstr + (p - str->str);

    n = 0;
    for (q = p; q; p = q) {
        if (repllen) {
            memcpy(newp, replacement, repllen);
            newp += repllen;
        }

        if (++n == ucount)
            break;
        if (!(q = strstr(q + oldlen, old)))
            break;

        memcpy(newp, p + oldlen, (q - p) - oldlen);
        newp += (q - p) - oldlen;
    }

    memcpy(newp, p + oldlen, str->len - oldlen - (p - str->str));

    g_string_truncate(str, 0);
    g_string_append_len(str, newstr, newlen);
    g_free(newstr);

    return ucount;
}

/**
 * gwy_gstring_to_native_eol:
 * @str: A #GString string to modify in place.
 *
 * Converts "\n" in a string to operating system native line terminators.
 *
 * Text files are most easily written by opening them in the text mode.  This function can be useful for writing text
 * files using functions such as g_file_set_contents() that do not permit the conversion to happen automatically.
 *
 * It is a no-op on all POSIX systems, including OS X.  So at present, it actually performs any conversion at all only
 * on MS Windows.
 *
 * Since: 2.36
 **/
void
gwy_gstring_to_native_eol(GString *str)
{
    g_return_if_fail(str);
#ifdef G_OS_WIN32
    gwy_gstring_replace(str, "\n", "\r\n", -1);
#endif
}

/**
 * gwy_utf16_to_utf8:
 * @str: A UTF-16 encoded string.
 * @len: Maximum string length (number of #gunichar2 items in @str, NOT the number of bytes).  May be -1 if @str is
 *       nul-terminated.
 * @byteorder: Byte order of @str.
 *
 * Convert a string from UTF-16 to UTF-8. The result will be terminated with a 0 byte.
 *
 * This functions differs from g_utf16_to_utf8() mainly by the handling of byte order.  In particular, the caller
 * specifies the byte order explicitly and it can differ from the native byte order.
 *
 * It is possible to pass %GWY_BYTE_ORDER_IMPLICIT as @byteorder.  In such case @str is checked for a byte-order mark.
 * When one is present it is used for the byte order.  Otherwise the behaviour is the same as for
 * %GWY_BYTE_ORDER_NATIVE.  The output string never begins with a byte-order mark.
 *
 * Conversion failure is indicated just by a %NULL return value.  Conversion is either completely successful or fails.
 * Use g_utf16_to_utf8() if you need to handle partial conversions and failure details.
 *
 * Returns: A newly allocated string on success; %NULL on failure.
 *
 * Since: 2.60
 **/
gchar*
gwy_utf16_to_utf8(const gunichar2 *str,
                  glong len,
                  GwyByteOrder byteorder)
{
    gboolean byteswap = (byteorder && byteorder != G_BYTE_ORDER && byteorder != GWY_BYTE_ORDER_IMPLICIT);
    gunichar2 maybebom;
    gunichar2 *swapped;
    gchar *retval;
    gboolean isrealbom = FALSE;

    /* Whatever... */
    if (!str)
        return NULL;

    if (len < 0) {
        while (str[len])
            len++;
    }
    if (!len)
        return g_strdup("");

    maybebom = str[0];
    if (maybebom == 0xfeff || maybebom == 0xfffe) {
        if (byteorder == GWY_BYTE_ORDER_IMPLICIT) {
            isrealbom = TRUE;
            byteswap = (maybebom == 0xfffe);
        }
        else if (byteswap && maybebom == 0xfffe)
            isrealbom = TRUE;
        else if (!byteswap && maybebom == 0xfeff)
            isrealbom = TRUE;
        /* In all other cases the caller is perhaps confused, but the conversion should fail.  Do not guess. */
    }
    if (isrealbom) {
        str++;
        len--;
    }

    if (!byteswap)
        return g_utf16_to_utf8(str, len, NULL, NULL, NULL);

    swapped = g_new(gunichar2, len+1);
    gwy_memcpy_byte_swap((const guint8*)str, (guint8*)swapped, 2, len, 1);
    swapped[len] = 0;
    retval = g_utf16_to_utf8(swapped, len, NULL, NULL, NULL);
    g_free(swapped);

    return retval;
}

/**
 * gwy_utf8_strescape:
 * @source: A string to escape.
 * @exceptions: A string containing characters to not escape, or %NULL.
 *
 * Escapes special characters in a string.
 *
 * Escapes the special characters '\b', '\f', '\n', '\r', '\t', '\v', '\' and '"' in the string source by inserting
 * a '\' before them. Additionally all characters in the range 0x01-0x1F (everything below SPACE), the character 0x7F,
 * characters 0xC0 and 0xC1, and all characters in the range 0xF5-0xFF are replaced with a '\' followed by their octal
 * representation. Characters supplied in exceptions are not escaped.
 *
 * In essence, this functions differs from g_strescape() by preserving valid UTF-8 strings, unless they contain ‘odd’
 * characters. Passing strings which are not valid UTF-8 is possible, but generally not advised.
 *
 * Function g_strcompress() does the reverse conversion.
 *
 * Returns: A newly allocated string with special characters escaped.
 *
 * Since: 2.62
 **/
gchar*
gwy_utf8_strescape(const gchar *source,
                   const gchar *exceptions)
{
    static const gchar stdescapes[] = { 'b', 't', 'n', 'v', 'f', 'r' };
    const guchar *p = (guchar*)source;
    guchar *dest, *q;
    guchar c, excmap[256];

    g_return_val_if_fail(source, NULL);

    /* Each source byte needs maximally four destination chars (\777) */
    q = dest = g_malloc(4*strlen(source) + 1);

    if (exceptions && *exceptions) {
        guchar *e = (guchar*)exceptions;

        gwy_clear(excmap, 256);
        do {
            excmap[*(e++)] = 1;
        } while (*e);
    }
    else
        exceptions = NULL;

    while ((c = *p)) {
        if (exceptions && excmap[c])
            *(q++) = c;
        else if (c >= '\b' && c <= '\r') {
            *(q++) = '\\';
            *(q++) = stdescapes[c - '\b'];
        }
        else if (c == '\\' || c == '"') {
            *(q++) = '\\';
            *(q++) = c;
        }
        else if (c < ' ' || c == 0x7f || c == 0xc0 || c == 0xc1 || c >= 0xf5) {
            *(q++) = '\\';
            *(q++) = '0' + ((c >> 6) & 0x07);
            *(q++) = '0' + ((c >> 3) & 0x07);
            *(q++) = '0' + (c & 0x07);
        }
        else
            *(q++) = c;
        p++;
    }
    *q = 0;

    return dest;
}

/**
 * gwy_convert_to_utf8:
 * @str: A string to convert to UTF-8.
 * @len: Maximum string length.  May be -1 if @str is NUL-terminated.
 * @codeset: String code set, for instance "ISO-8859-1" or "CP1251".
 *
 * Converts a string to UTF-8.
 *
 * This is a g_convert() wrapper for the typical SPM file reading use case. Conversion failure is indicated just by
 * a %NULL return value.  Conversion is either completely successful or fails. Use g_convert() or g_iconv() if you
 * need to handle partial conversions and failure details.
 *
 * Returns: A newly allocated string on success; %NULL on failure.
 *
 * Since: 2.64
 **/
gchar*
gwy_convert_to_utf8(const guchar *str,
                    glong len,
                    const gchar *codeset)
{
    return g_convert(str, len, "UTF-8", codeset, NULL, NULL, NULL);
}

/**
 * gwy_assign_string:
 * @target: Pointer to target string, typically a struct field.
 * @newvalue: New value of the string, may be %NULL.
 *
 * Assigns a string, checking for equality and handling %NULL<!-- -->s.
 *
 * This function simplifies handling of string value setters.
 *
 * The new value is duplicated and the old string is freed in a safe manner (it is possible to pass a pointer
 * somewhere within the old value as the new value, for instance).  Any of the old and new value can be %NULL.  If
 * both values are equal (including both unset), the function returns %FALSE.
 *
 * Returns: %TRUE if the target string has changed.
 *
 * Since: 2.59
 **/
gboolean
gwy_assign_string(gchar **target,
                  const gchar *newvalue)
{
    if (*target && newvalue) {
        if (!gwy_strequal(*target, newvalue)) {
            gchar *old = *target;
            *target = g_strdup(newvalue);
            g_free(old);
            return TRUE;
        }
    }
    else if (*target) {
        g_free(*target);
        *target = NULL;
        return TRUE;
    }
    else if (newvalue) {
        *target = g_strdup(newvalue);
        return TRUE;
    }
    return FALSE;
}

/**
 * gwy_strequal:
 * @a: A string.
 * @b: Another string.
 *
 * Expands to %TRUE if strings are equal, to %FALSE otherwise.
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

