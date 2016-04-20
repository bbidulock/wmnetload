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
 * Solaris-specific interface statistics gathering routines.
 */

#pragma ident "@(#)ifstat_solaris.c	1.2	02/01/22 meem"

/*
 * TODO: Gather statisitcs for the loopback interface.
 */

#include <config.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <ctype.h>
#include <kstat.h>
#include <stdlib.h>
#include <string.h>

#include "ifstat.h"
#include "utils.h"

struct ifstatstate {
	kstat_ctl_t		*kcp;		/* kstat instance pointer */
};

/*
 * Do one-time setup stuff for accessing the interface statistics and store
 * the gathered information in an interface statistics state structure.
 * Return the state structure.
 */
ifstatstate_t *
if_statinit(void)
{
	ifstatstate_t	*statep;

	statep = calloc(1, sizeof (ifstatstate_t));
	if (statep == NULL) {
		warn("cannot allocate interface statistics state");
		return (NULL);
	}

	statep->kcp = kstat_open();
	if (statep->kcp == NULL) {
		warn("cannot access kstats; no stats will be available\n");
		free(statep);
		return (NULL);
	}

	return (statep);
}

/*
 * Optionally using state stored in `statep', retrieve stats on interface
 * `ifname', and store the statistics in `ifstatsp'.
 */
int
if_stats(const char *ifname, ifstatstate_t *statep, ifstats_t *ifstatsp)
{
	kstat_t		*ksp;
	kstat_named_t	*krp, *ktp;
	const char	*ifnamep;
	char		ifbuf[IFNAMSIZ];

	ifnamep = ifname + strlen(ifname) - 1;
	while (&ifnamep[-1] >= ifname && isdigit(ifnamep[-1]))
		ifnamep--;

	(void) strncpy(ifbuf, ifname, IFNAMSIZ);
	ifbuf[ifnamep - ifname] = '\0';

	ksp = kstat_lookup(statep->kcp, ifbuf, atoi(ifnamep), (char *)ifname);
	if (ksp == NULL)
		return (0);

	if (kstat_read(statep->kcp, ksp, NULL) == -1)
		return (0);

	krp = kstat_data_lookup(ksp, "rbytes");
	ktp = kstat_data_lookup(ksp, "obytes");
	if (krp == NULL || ktp == NULL)
		return (0);

	ifstatsp->rxbytes = krp->value.ul;
	ifstatsp->txbytes = ktp->value.ul;

	return (1);
}

/*
 * Clean up the interface state structure pointed to by `statep'.
 */
void
if_statfini(ifstatstate_t *statep)
{
	(void) kstat_close(statep->kcp);
	free(statep);
}
