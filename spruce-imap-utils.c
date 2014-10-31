/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* excerpts from: https://github.com/jstedfast/spruce/blob/master/spruce/providers/imap/spruce-imap-utils.c#L89 */

/*  Spruce
 *  Copyright (C) 1999-2009 Jeffrey Stedfast
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
 *  Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>

// #include <spruce/spruce-error.h>

/*
#include "spruce-imap-engine.h"
#include "spruce-imap-stream.h"
#include "spruce-imap-command.h"
*/

#include "spruce-imap-utils.h"

#define d(x) x


static char *utf7_alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";

static unsigned char utf7_rank[256] = {
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x3e,0x3f,0xff,0xff,0xff,
	0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,
	0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0xff,0xff,0xff,0xff,0xff,
	0xff,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,
	0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,0x30,0x31,0x32,0x33,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
};

static inline void
spruce_utf8_putc (unsigned char **outbuf, gunichar u)
{
	register unsigned char *outptr = *outbuf;

	if (u <= 0x7f)
		*outptr++ = u;
	else if (u <= 0x7ff) {
		*outptr++ = 0xc0 | u >> 6;
		*outptr++ = 0x80 | (u & 0x3f);
	} else if (u <= 0xffff) {
		*outptr++ = 0xe0 | u >> 12;
		*outptr++ = 0x80 | ((u >> 6) & 0x3f);
		*outptr++ = 0x80 | (u & 0x3f);
	} else {
		/* see unicode standard 3.0, S 3.8, max 4 octets */
		*outptr++ = 0xf0 | u >> 18;
		*outptr++ = 0x80 | ((u >> 12) & 0x3f);
		*outptr++ = 0x80 | ((u >> 6) & 0x3f);
		*outptr++ = 0x80 | (u & 0x3f);
	}

	*outbuf = outptr;
}

char *
spruce_imap_utf7_utf8 (const char *in)
{
	const unsigned char *inptr = (unsigned char *) in;
	unsigned char c;
	int shifted = 0;
	guint32 v = 0;
	GString *out;
	gunichar u;
	char *buf;
	int i = 0;

	out = g_string_new ("");

	while (*inptr) {
		c = *inptr++;

		if (shifted) {
			if (c == '-') {
				/* shifted back to US-ASCII */
				shifted = 0;
			} else {
				/* base64 decode */
				if (utf7_rank[c] == 0xff)
					goto exception;

				v = (v << 6) | utf7_rank[c];
				i += 6;

				if (i >= 16) {
					u = (v >> (i - 16)) & 0xffff;
					g_string_append_unichar (out, u);
					i -= 16;
				}
			}
		} else if (c == '&') {
			if (*inptr == '-') {
				g_string_append_c (out, '&');
				inptr++;
			} else {
				/* shifted to modified UTF-7 */
				shifted = 1;
			}
		} else {
			g_string_append_c (out, c);
		}
	}

	if (shifted) {
	exception:
		g_warning ("Invalid UTF-7 encoded string: '%s'", in);
		g_string_free (out, TRUE);
		return g_strdup (in);
	}

	buf = out->str;
	g_string_free (out, FALSE);

	return buf;
}


static inline gunichar
spruce_utf8_getc (const unsigned char **in)
{
	register const unsigned char *inptr = *in;
	register unsigned char c, r;
	register gunichar m, u = 0;

	if (*inptr == '\0')
		return 0;

	r = *inptr++;
	if (r < 0x80) {
		*in = inptr;
		u = r;
	} else if (r < 0xfe) { /* valid start char? */
		u = r;
		m = 0x7f80;	/* used to mask out the length bits */
		do {
			c = *inptr++;
			if ((c & 0xc0) != 0x80)
				goto error;

			u = (u << 6) | (c & 0x3f);
			r <<= 1;
			m <<= 5;
		} while (r & 0x40);

		*in = inptr;

		u &= ~m;
	} else {
	error:
		*in = (*in) + 1;
		u = 0xfffe;
	}

	return u;
}

static void
utf7_close (GString *out, guint32 u2, int i)
{
	guint32 x;

	if (i > 0) {
		x = (u2 << (6 - i)) & 0x3f;
		g_string_append_c (out, utf7_alphabet[x]);
	}

	g_string_append_c (out, '-');
}

char *
spruce_imap_utf8_utf7 (const char *in)
{
	const unsigned char *inbuf = (unsigned char *) in;
	const unsigned char *inptr = (unsigned char *) in;
	guint32 x, u2 = 0;
	int shifted = 0;
	GString *out;
	gunichar u;
	char *buf;
	int i = 0;

	out = g_string_new ("");
	while ((u = spruce_utf8_getc (&inptr))) {
		if (u == 0xfffe) {
			char *where;

			where = g_alloca (inptr - inbuf);
			memcpy (where, "-", (inptr - inbuf) - 1);
			where[(inptr - inbuf) - 1] = '\0';

			g_warning ("Invalid UTF-8 sequence encountered in '%s'\n"
				   "                                       %s^", in, where);
			continue;
		}

		if (u >= 0x20 && u <= 0x7e) {
			/* characters with octet values 0x20-0x25 and 0x27-0x7e
			   represent themselves while 0x26 ("&") is represented
			   by the two-octet sequence "&-" */
			if (shifted) {
				utf7_close (out, u2, i);
				shifted = 0;
				i = 0;
			}

			if (u == 0x26)
				g_string_append (out, "&-");
			else
				g_string_append_c (out, (char) u);
		} else {
			/* base64 encode */
			if (!shifted) {
				g_string_append_c (out, '&');
				shifted = 1;
			}

			u2 = (u2 << 16) | (u & 0xffff);
			i += 16;

			while (i >= 6) {
				x = (u2 >> (i - 6)) & 0x3f;
				g_string_append_c (out, utf7_alphabet[x]);
				i -= 6;
			}
		}
	}

	if (shifted)
		utf7_close (out, u2, i);

	buf = out->str;
	g_string_free (out, FALSE);

	return buf;
}


