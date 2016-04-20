/*
 * Copyright (c) 2003 Peter Memishian (meem) <meem@gnu.org> and
 *                    Hendrik Scholz <hscholz at raisdorf.net>
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
 * FreeBSD-specific interface statistics gathering routines.
 */

#pragma ident "%Z%%M%	%I%	%E% meem"

#include <config.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_mib.h>

#include "ifstat.h"
#include "utils.h"

struct ifstatstate {
	int		ifindex;	/* cached row index of the interface */
};

/*
 * Do one-time setup stuff for accessing the interface statistics and store
 * the gathered information in an interface statistics state structure.
 * Return the state structure.
 */
ifstatstate_t *
if_statinit(void)
{
	ifstatstate_t   *statep;

	statep = malloc(sizeof (ifstatstate_t));
	if (statep == NULL) {
		warn("cannot allocate interface statistics state");
		return (NULL);
	}

	statep->ifindex = 1;	/* just a guess */
	return (statep);
}

/*
 * Optionally using state stored in `statep', retrieve stats on interface
 * `ifname', and store the statistics in `ifstatsp'.
 */
int
if_stats(const char *ifname, ifstatstate_t *statep, ifstats_t *ifstatsp)
{
	int row;
	int name[6] = { CTL_NET, PF_LINK, NETLINK_GENERIC, IFMIB_IFDATA };
	size_t len;
	struct ifmibdata ifmd;

	name[4] = statep->ifindex;
	name[5] = IFDATA_GENERAL;

	len = sizeof (ifmd);
	if ((sysctl(name, 6, &ifmd, &len, NULL, 0) == -1) ||
	    (strcmp(ifmd.ifmd_name, ifname) != 0)) {
		/*
		 * Interface index has changed; find it again.
		 */
		len = sizeof (name[4]);
		if (sysctlbyname("net.link.generic.system.ifcount", &name[4],
		    &len, NULL, 0) == -1) {
			warn("cannot retrieve the interface count");
			return (0);
		}

		len = sizeof (ifmd);
		for (; name[4] > 0; name[4]--) {
			if (sysctl(name, 6, &ifmd, &len, NULL, 0) == -1)
				break;

			if (strcmp(ifmd.ifmd_name, ifname) == 0) {
				statep->ifindex = name[4];
				goto done;
			}
		}

		warn("cannot retrieve interface statistics");
		return (0);
	}
done:
	ifstatsp->rxbytes = ifmd.ifmd_data.ifi_ibytes;
	ifstatsp->txbytes = ifmd.ifmd_data.ifi_obytes;
	return (1);
}

/*
 * Clean up the interface state structure pointed to by `statep'.
 */
void
if_statfini(ifstatstate_t *statep)
{
	free(statep);
}
