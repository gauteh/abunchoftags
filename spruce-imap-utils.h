/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* excerpts from: https://github.com/jstedfast/spruce/blob/master/spruce/providers/imap/spruce-imap-utils.h */

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


#ifndef __SPRUCE_IMAP_UTILS_H__
#define __SPRUCE_IMAP_UTILS_H__

#include <glib.h>

G_BEGIN_DECLS

/* IMAP UTF-7 folder name encoding */
char *spruce_imap_utf7_utf8 (const char *in);
char *spruce_imap_utf8_utf7 (const char *in);

G_END_DECLS

#endif /* __SPRUCE_IMAP_UTILS_H__ */
