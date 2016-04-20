/*
 * Copyright (c) 2002 Peter Memishian (meem) <meem@gnu.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * wmnetload - A dockapp to monitor network interface usage.
 *	       Inspired by Seiichi SATO's nifty CPU usage monitor.
 *
 * Generic utility function interfaces.
 */

#ifndef	WN_UTILS_H
#define	WN_UTILS_H

#pragma ident "@(#)utils.h	1.2	02/01/22 meem"

#include <stdarg.h>

extern const char	*progname;

typedef enum { PRIV_DROP, PRIV_GAIN } privmode_t;

extern void		warn(const char *, ...);
extern void		die(const char *, ...);
extern void		chpriv(privmode_t);

#endif /* WN_UTILS_H */
