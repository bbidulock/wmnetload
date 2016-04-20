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
 * NetBSD-specific interface statistics gathering routines.
 */

#pragma ident "@(#)ifstat_netbsd.c	1.2	02/01/29 meem"

#include <config.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>
#include <stdlib.h>
#include <string.h>

#include "ifstat.h"
#include "utils.h"

struct ifstatstate {
	void	*ifnet_head;
	kvm_t	*kd;
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
	struct nlist	ifnet[] = { { "_ifnet" }, { NULL }};
	char		errbuf[_POSIX2_LINE_MAX];

	statep = malloc(sizeof (ifstatstate_t));
	if (statep == NULL) {
		warn("cannot allocate interface statistics state");
		return (NULL);
	}

	/*
	 * Just for the duration of kmem_openfiles(), get privileges
	 * needed to access kmem.
	 */
	chpriv(PRIV_GAIN);
	statep->kd = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, errbuf);
	chpriv(PRIV_DROP);
	if (statep->kd == NULL) {
		warn("cannot access raw kernel memory: %s\n", errbuf);
		free(statep);
		return (NULL);
	}

	if (kvm_nlist(statep->kd, ifnet) == -1) {
		warn("cannot populate kernel namelist: %s\n",
		    kvm_geterr(statep->kd));
		goto fail;
	}

	if (kvm_read(statep->kd, ifnet->n_value, &statep->ifnet_head,
	    sizeof (ifnet->n_value)) != sizeof (ifnet->n_value)) {
		warn("cannot find ifnet list head: %s\n",
		    kvm_geterr(statep->kd));
		goto fail;
	}

	return (statep);
fail:
	(void) kvm_close(statep->kd);
	free(statep);
	return (NULL);
}

/*
 * Optionally using state stored in `statep', retrieve stats on interface
 * `ifname', and store the statistics in `ifstatsp'.
 */
int
if_stats(const char *ifname, ifstatstate_t *statep, ifstats_t *ifstatsp)
{
	void		*ifnet_addr = statep->ifnet_head;
	struct ifnet	ifnet;

	for (; ifnet_addr != NULL; ifnet_addr = TAILQ_NEXT(&ifnet, if_list)) {

		if (kvm_read(statep->kd, (unsigned long)ifnet_addr, &ifnet,
		    sizeof (struct ifnet)) != sizeof (struct ifnet))
			return (0);

		if (strcmp(ifnet.if_xname, ifname) == 0) {
			ifstatsp->rxbytes = ifnet.if_ibytes;
			ifstatsp->txbytes = ifnet.if_obytes;
			return (1);
		}
	}

	return (0);
}

/*
 * Clean up the interface state structure pointed to by `statep'.
 */
void
if_statfini(ifstatstate_t *statep)
{
	(void) kvm_close(statep->kd);
	free(statep);
}
