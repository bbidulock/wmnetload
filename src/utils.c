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
 * Generic utility functions.
 */

#pragma ident "@(#)utils.c	1.4	02/10/27 meem"

#include <config.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

const char *progname;

/* PRINTFLIKE1 */
void
warn(const char *format, ...)
{
	va_list alist;
	char *errstr = strerror(errno);

	if (errstr == NULL)
		errstr = "<unknown error>";

	(void) fprintf(stderr, "%s: ", progname);

	va_start(alist, format);
	(void) vfprintf(stderr, format, alist);
	va_end(alist);

	if (strrchr(format, '\n') == NULL)
		(void) fprintf(stderr, ": %s\n", errstr);
}

/* PRINTFLIKE1 */
void
die(const char *format, ...)
{
	va_list alist;
	char *errstr = strerror(errno);

	if (errstr == NULL)
		errstr = "<unknown error>";

	(void) fprintf(stderr, "%s: fatal: ", progname);

	va_start(alist, format);
	(void) vfprintf(stderr, format, alist);
	va_end(alist);

	if (strrchr(format, '\n') == NULL)
		(void) fprintf(stderr, ": %s\n", errstr);

	exit(EXIT_FAILURE);
}

void
chpriv(privmode_t privmode)
{
	static gid_t	saved_egid;
	static uid_t	saved_euid;
	static int	is_initialized;
	gid_t		new_egid;
	uid_t		new_euid;

	if (is_initialized == 0) {
		saved_euid = geteuid();
		saved_egid = getegid();
		is_initialized = 1;
	}

	switch (privmode) {
	case PRIV_DROP:
		new_euid = getuid();
		new_egid = getgid();
		break;

	case PRIV_GAIN:
		new_euid = saved_euid;
		new_egid = saved_egid;
		break;

	default:
		die("unknown privmode value %d", privmode);
	}

	if (geteuid() != new_euid) {
		if (seteuid(new_euid))
			die("cannot set effective uid to %d", new_euid);
	}

	if (getegid() != new_egid) {
		if (setegid(new_egid))
			die("cannot set effective gid to %d", new_egid);
	}
}
