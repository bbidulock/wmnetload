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
 * Linux-specific interface statistics gathering routines.
 */

#pragma ident "@(#)ifstat_linux.c	1.3	02/10/27 meem"

#include <config.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ifstat.h"
#include "utils.h"

#define	WN_PND_MAX	1024		/* see rant below */

struct ifstatstate {
	int		rbindex;	/* receive byte count index */
	int		tbindex;	/* transmit byte count index */
	FILE		*fp;
};

/*
 * Do one-time setup stuff for accessing the interface statistics and store
 * the gathered information in an interface statistics state structure.
 * Return the state structure.
 */
ifstatstate_t *
if_statinit(void)
{
	char		line[WN_PND_MAX];
	const char	*seps = " :\t|";
	char		*token;
	unsigned int	i;
	ifstatstate_t	*statep;

	statep = malloc(sizeof (ifstatstate_t));
	if (statep == NULL) {
		warn("cannot allocate interface statistics state");
		return (NULL);
	}

	/*
	 * This has to be one of the most vile interfaces ever conceived.
	 * In a word: pathetic.  The Linux community should be ashamed.
	 */
	statep->fp = fopen("/proc/net/dev", "r");
	if (statep->fp == NULL)
		goto openfail;

	statep->rbindex = -1;
	statep->tbindex = -1;

	/*
	 * Find the line with the column headers.
	 */
	for (;;) {
		if (fgets(line, sizeof (line), statep->fp) == NULL)
			goto parsefail;

		if (strstr(line, "bytes") != NULL)
			break;
	}

	/*
	 * Figure out which columns are associated with which statistics;
	 * blithely assume that "receive" is before "transmit".
	 */
	token = strtok(line, seps);
	for (i = 0; token != NULL; i++) {
		if (strcmp(token, "bytes") == 0) {
			if (statep->rbindex == -1)
				statep->rbindex = i;
			else
				statep->tbindex = i;
		}
		token = strtok(NULL, seps);
	}

	if (statep->rbindex == -1 || statep->tbindex == -1)
		goto parsefail;

	return (statep);

openfail:
	free(statep);
	warn("cannot open /proc/net/dev; no stats will be available\n");
	return (NULL);

parsefail:
	(void) fclose(statep->fp);
	free(statep);
	warn("cannot parse /proc/net/dev; no stats will be available\n");
	return (NULL);
}

/*
 * Optionally using state stored in `statep', retrieve stats on interface
 * `ifname', and store the statistics in `ifstatsp'.
 */
int
if_stats(const char *ifname, ifstatstate_t *statep, ifstats_t *ifstatsp)
{
	char		line[WN_PND_MAX];
	const char	*seps = " :\t|";
	char		*token;
	unsigned int	i;

	statep->fp = freopen("/proc/net/dev", "r", statep->fp);
	if (statep->fp == NULL)
		return (0);

	do {
		if (fgets(line, sizeof (line), statep->fp) == NULL)
			return (0);

		token = strtok(line, seps);
		if (token == NULL)
			return (0);
	} while (strcmp(token, ifname) != 0);

	/*
	 * Found the right line; just read in the stats.
	 */
	for (i = 1; token != NULL; i++) {
		token = strtok(NULL, seps);

		if (i == statep->rbindex)
			ifstatsp->rxbytes = strtoll(token, NULL, 0);
		else if (i == statep->tbindex)
			ifstatsp->txbytes = strtoll(token, NULL, 0);

		if (i >= statep->rbindex && i >= statep->tbindex)
			return (1);
	}

	return (0);
}

/*
 * Clean up the interface state structure pointed to by `statep'.
 */
void
if_statfini(ifstatstate_t *statep)
{
	(void) fclose(statep->fp);
	free(statep);
}
