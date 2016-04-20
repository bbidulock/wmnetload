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
 * Network statistics interfaces -- to be implemented by each flavor
 * of Unix that wmnetload runs on.
 */

#ifndef	WN_IFSTAT_H
#define	WN_IFSTAT_H

#pragma ident "@(#)ifstat.h	1.1	02/01/09 meem"

/*
 * For now, we only keep two network statistics -- maybe we'll keep more in
 * the future.
 */
typedef struct {
	unsigned long long	rxbytes;	/* received byte count */
	unsigned long long	txbytes;	/* transmitted byte count */
} ifstats_t;

/*
 * Each network statistics implementation must define its own version of
 * this structure.
 */
typedef struct ifstatstate ifstatstate_t;

extern ifstatstate_t	*if_statinit(void);
extern int		if_stats(const char *, ifstatstate_t *, ifstats_t *);
extern void		if_statfini(ifstatstate_t *);

#endif /* WN_IFSTAT_H */
