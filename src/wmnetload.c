/*
 * Copyright (c) 2002-2003 Peter Memishian (meem) <meem@gnu.org>
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
 */

#pragma ident "@(#)wmnetload.c	1.15	03/02/23 meem"

#include <config.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <dockapp.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include "ifstat.h"
#include "utils.h"
#include "pixmaps.h"

/*
 * Convenience macro for copying rectangles with shared origins.
 */
#define	WN_XCopyArea(s, d, w, h, x, y) \
	XCopyArea(DADisplay, s, d, DAGC, x, y, w, h, x, y)

/*
 * Make it easy to increment and decrement an integer modulo a value.
 */
#define	WN_MODDEC(n, mod) ((n) == 0 ? (mod - 1) : (n - 1))
#define	WN_MODINC(n, mod) ((n + 1) % mod)

/*
 * Default scale for network activity graph: graph can display up
 * to 150 kbytes/sec (we then scale by multiples of 2).
 */
#define	WN_DEF_BPS2BAR	(150 * 125 / WN_COL_HEIGHT)

/*
 * Flags for draw_dockapp().
 */
enum {
	WN_DRAWBPS	= 0x01,	/* Draw just the bps value */
	WN_DRAWGRAPH	= 0x02,	/* Draw just the graph */
	WN_DRAWIFNAME	= 0x04, /* Draw interface name */
	WN_DRAWALL	= 0xff	/* Draw everything */
};

/*
 * Flags set by buttonpress().
 */
enum {
	WN_BP_NEXTIF	= 0x01,	/* cycle to next interface */
	WN_BP_REDRAW	= 0x02	/* redraw dockapp */
};

/*
 * Flags associated with the display.
 */
enum {
	WN_DISP_LIGHT	= 0x01,	/* use backlight */
	WN_DISP_WARN	= 0x02,	/* warn the user */
	WN_DISP_IFNAME	= 0x04, /* show interface name (if possible) */
	WN_DISP_INBYTES	= 0x08,	/* display in bytes (instead of bits) */
	WN_DISP_ALARM	= 0x10,	/* use visual alarm */
	WN_DISP_BACKLIT	= 0x11	/* WN_DISP_ALARM | WN_DISP_LIGHT */
};

typedef enum { IF_UNKNOWN, IF_UP, IF_DOWN } ifstatus_t;

typedef struct {
	ulonglong_t	bps2bar;		/* bps -> bar conversion */
	ulonglong_t	rbars[WN_GR_COLS];	/* receive bars */
	ulonglong_t	tbars[WN_GR_COLS];	/* transmit bars */
	ifstats_t	stats[WN_GR_COLS];	/* unscaled stats, in bps */
	unsigned int	col;			/* current column in graph */
	unsigned int	maxcol;			/* column controlling bps2bar */
} ifgraph_t;

typedef struct {
	char		*name;			/* interface name */
	ifstatus_t	status;			/* current status */
	ulonglong_t	bps;			/* current bps */
	ifgraph_t	*graph;			/* interface stats graph */
	ifstatstate_t	*statep;		/* pointer to interface state */
} ifinfo_t;

static void	draw_bps(ulonglong_t, Pixmap);
static void	draw_digit(unsigned int, unsigned int, Pixmap);
static void	draw_decimal(unsigned int, Pixmap);
static void	draw_speed(unsigned int, Pixmap);
static void	draw_graph(ifgraph_t *, Pixmap);
static void	draw_dockapp(ifinfo_t *, unsigned int, Pixmap);
static void	draw_ifname(const char *, Pixmap);
static void	setshape(void);
static int	xpm2pixmap(void);
static void	buttonpress(int, int, int, int);
static ifinfo_t *ifinfo_create(const char *);
static void	ifinfo_destroy(ifinfo_t *);
static void	ifinfo_monitor(ifinfo_t *, int, unsigned int, int, Pixmap);
static ifstatus_t if_status(int, const char *);
static int	if_flags(int, const char *);
static int	if_next(int, const char *, char *);
static void	next_bps(double [], unsigned int, unsigned int, ifinfo_t *);
static unsigned int next_timeout(unsigned int[], unsigned int,
    struct timeval *, unsigned int);
static double *smoothtable_init(unsigned int);
static unsigned int *timetable_init(double [], unsigned int, unsigned int);
static unsigned long getblendedcolor(const char *, int);

static Pixmap backlight_on, backlight_off, backlight_err, backlight_down;
static Pixmap backlight_down_on, backlight_down_off, parts, font;

static char *desc = "\nNetwork interface usage monitor.\n";
static char *vers = "wmnetload "VERSION" by meem@gnu.org -- compiled "__DATE__;

enum { OPT_DISPLAY, OPT_BACKLIGHT, OPT_LIGHTCOLOR, OPT_UPDATE, OPT_INTERFACE,
       OPT_NOIFNAME, OPT_SMOOTHING, OPT_BYTES, OPT_ALARM, OPT_KEEP, OPT_MAX };

extern int d_windowed;		/* grr; should be in <dockapp.h> */

static DAProgramOption options[] = {
	{ "-d", "--display", "sets display to use", DOString },
	{ "-bl", "--backlight", "turns on backlight", DONone },
	{ "-lc", "--lightcolor", "sets backlight color (default: #6EC63B)",
	  DOString },
	{ "-u", "--update", "sets update interval (in seconds)", DOInteger },
	{ "-i", "--interface", "sets interface to monitor", DOString },
	{ "-n", "--no-ifname", "does not display interface name", DONone },
	{ "-s", "--smooth", "sets smoothing value (experimental)", DOInteger },
	{ "-b", "--bytes", "display bytes/sec instead of bits/sec", DONone },
	{ "-a", "--alarm", "activates alarm mode. <number> is in kbits/sec\n"
	  "\t\t\t\t(or kbytes/sec if -b is specified)", DOInteger },
	{ "-k", "--keep-ifname", "keep interface name even if not found",
	  DONone }
};

static DACallbacks callbacks = { NULL, buttonpress };

/*
 * Gross global data -- sigh.  No choice since libdockapp doesn't
 * currently let us pass private data to its callbacks.
 */
static unsigned int	dispflags;	/* current display flags */
static unsigned int	bpflags;	/* flags set by buttonpress() */
static double		*smoothtable;
static unsigned int	*timetable;
static ulonglong_t	alarmthresh;	/* in bits per second; 0 = none */
static char		*lightcolor;

int
main(int argc, char **argv)
{
	XTextProperty	name;
	char		nextifname[IFNAMSIZ];
	char		*ifname;
	char		*display;
	int		niter;
	int		interval;
	int		alarm;
	int		siocfd;
	ifinfo_t	*ifp;
	Pixmap		pixmap;

	bzero(nextifname, IFNAMSIZ);
	progname = strrchr(argv[0], '/');
	if (progname != NULL)
		progname++;
	else
		progname = argv[0];

	chpriv(PRIV_DROP);

	options[OPT_DISPLAY].value.string	= &display;
	options[OPT_INTERFACE].value.string	= &ifname;
	options[OPT_UPDATE].value.integer	= &interval;
	options[OPT_SMOOTHING].value.integer	= &niter;
	options[OPT_ALARM].value.integer	= &alarm;
	options[OPT_LIGHTCOLOR].value.string	= &lightcolor;

	DAParseArguments(argc, argv, options, OPT_MAX, desc, vers);

	siocfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (siocfd == -1)
		die("cannot open datagram socket");

	if (!if_next(siocfd, NULL, nextifname))
		die("no interfaces available\n");

	if (!options[OPT_INTERFACE].used) {
		ifname = nextifname;
	} else if (if_status(siocfd, ifname) == IF_UNKNOWN) {
		if (!options[OPT_KEEP].used) {
			warn("unknown interface %s; defaulting to %s\n", ifname,
			    nextifname);
			ifname = nextifname;
		}
	}

	if (!options[OPT_DISPLAY].used)
		display = "";

	if (!options[OPT_UPDATE].used)
		interval = 1;

	if (options[OPT_BACKLIGHT].used)
		dispflags |= WN_DISP_LIGHT;

	if (!options[OPT_NOIFNAME].used)
		dispflags |= WN_DISP_IFNAME;

	if (options[OPT_BYTES].used)
		dispflags |= WN_DISP_INBYTES;

	if (options[OPT_ALARM].used) {
		if (options[OPT_BYTES].used)
			alarmthresh = alarm * 1000;
		else
			alarmthresh = alarm * 1000 / 8;
	}

	if (!options[OPT_SMOOTHING].used)
		niter = 1;

	smoothtable = smoothtable_init(niter);
	if (smoothtable == NULL)
		die("cannot create smoothing table");

	timetable = timetable_init(smoothtable, interval * 1000, niter);
	if (timetable == NULL)
		die("cannot create time smoothing table");

	DAInitialize(display, argv[0], WN_DA_WIDTH, WN_DA_HEIGHT, argc, argv);
	DASetCallbacks(&callbacks);

	/*
	 * Set our WM_NAME property, since DAInitialize() forgot to and it
	 * needs to be set so that AfterStep's wharf can swallow it.
	 */
	if (XStringListToTextProperty((char **)&progname, 1, &name))
		XSetWMName(DADisplay, DAWindow, &name);

	/*
	 * Set the shape of our window so that the corners get correctly
	 * masked.
	 */
	setshape();

	if (!xpm2pixmap())
		die("cannot load pixmaps from XPM data\n");

	pixmap = DAMakePixmap();
	DASetPixmap(pixmap);
	DAShow();

	/*
	 * NOTE: ifinfo_create() only returns if successful.
	 */
	ifp = ifinfo_create(ifname);
	for (;;) {
		ifinfo_monitor(ifp, siocfd, niter, interval, pixmap);
		if ((!options[OPT_KEEP].used) &&
		    (if_next(siocfd, ifname, nextifname))) {
			ifinfo_destroy(ifp);
			ifp = ifinfo_create(nextifname);
			ifname = nextifname;
		}
	}

	/* NOTREACHED */
	return (EXIT_SUCCESS);
}

static void
ifinfo_monitor(ifinfo_t *ifp, int siocfd, unsigned int niter, int interval,
    Pixmap pixbuf)
{
	struct timeval	start;
	ifstats_t	stats, ostats, *curstats;
	int		msec;
	unsigned int	iter;
	XEvent		event;
	ulonglong_t	realbps;

	ifp->status = if_status(siocfd, ifp->name);
	(void) if_stats(ifp->name, ifp->statep, &ostats);
	realbps = 0;

	draw_dockapp(ifp, WN_DRAWALL, pixbuf);

	for (;;) {
		(void) gettimeofday(&start, NULL);
		iter = 1;
		for (;;) {
			/*
			 * Skip the remaining smoothing iterations if
			 * we're already at the actual bps value.
			 */
			if (ifp->bps == realbps)
				iter = niter;

			msec = next_timeout(timetable, iter, &start,
			    interval * 1000);

			if (!DANextEventOrTimeout(&event, msec)) {
				if (++iter > niter)
					break;

				next_bps(smoothtable, iter, niter, ifp);
				draw_dockapp(ifp, WN_DRAWBPS, pixbuf);
			} else {
				DAProcessEvent(&event);
				if (bpflags & WN_BP_REDRAW)
					draw_dockapp(ifp, WN_DRAWALL, pixbuf);
				if (bpflags & WN_BP_NEXTIF) {
					bpflags = 0;
					return;
				}
				bpflags = 0;
			}
		}

		ifp->status = if_status(siocfd, ifp->name);

		if (if_stats(ifp->name, ifp->statep, &stats) == 0) {
			ifp->status = IF_UNKNOWN;
			stats = ostats;
		}

		ifp->graph->col = WN_MODINC(ifp->graph->col, WN_GR_COLS);
		curstats = &ifp->graph->stats[ifp->graph->col];
		curstats->rxbytes = (stats.rxbytes - ostats.rxbytes) / interval;
		curstats->txbytes = (stats.txbytes - ostats.txbytes) / interval;
		ostats = stats;

		realbps = curstats->rxbytes + curstats->txbytes;
		next_bps(smoothtable, 1, niter, ifp);
		/*
		 * XXX: This should really be WN_DRAWBPS | WN_DRAWGRAPH,
		 * but if we get covered up and then exposed, we don't get
		 * an expose event, so we don't know to redraw the
		 * interface name.  So, for now, we just pessimistically
		 * redraw the whole damn thing.  ARG!
		 */
		draw_dockapp(ifp, WN_DRAWALL, pixbuf);
	}
}

static unsigned int
next_timeout(unsigned int timetable[], unsigned int iter, struct timeval *start,
    unsigned int interval)
{
	struct timeval	elapsed, current;
	unsigned int	msspent;

	(void) gettimeofday(&current, NULL);

	elapsed.tv_sec  = current.tv_sec  - start->tv_sec;
	elapsed.tv_usec = current.tv_usec - start->tv_usec;
	if (elapsed.tv_usec < 0) {
		elapsed.tv_sec--;
		elapsed.tv_usec += 1000000; /* one second */
	}

	msspent = elapsed.tv_sec * 1000 + (elapsed.tv_usec / 1000);
	if (timetable[iter] + msspent > interval)
		return (0);

	return ((interval - msspent) - timetable[iter]);
}

/* XXX niter should be part of smoothtable */
static void
next_bps(double smoothtable[], unsigned int iter, unsigned int niter,
    ifinfo_t *ifp)
{
	ifstats_t	*stats = ifp->graph->stats;
	unsigned int	col = ifp->graph->col;
	unsigned int	ocol = WN_MODDEC(col, WN_GR_COLS);
	long long	num;
	long long	onum;

	num = stats[col].rxbytes + stats[col].txbytes;
	onum = stats[ocol].rxbytes + stats[ocol].txbytes;

	if (iter < niter)
		ifp->bps += (num - onum) * smoothtable[niter + 1 - iter];
	else
		ifp->bps = num;
}

/*
 * Handle buttonpress event.
 */
/* ARGSUSED */
static void
buttonpress(int button, int state, int x, int y)
{
	switch (button) {
	case 1:
		dispflags ^= WN_DISP_LIGHT;
		bpflags |= WN_BP_REDRAW;
		break;

	case 3:
		bpflags |= WN_BP_NEXTIF;
		break;
	}
}

/*
 * Based on the value in `flags', draw all or part of the dockapp in
 * `pixbuf', using the interface information contained in `ifp'.
 */
static void
draw_dockapp(ifinfo_t *ifp, unsigned int flags, Pixmap pixbuf)
{
	Pixmap		background = backlight_off;
	unsigned int	odispflags = dispflags;

	/*
	 * Enable or disable the alarm, as appropriate.
	 */
	if (alarmthresh > 0 && ifp->bps >= alarmthresh)
		dispflags |= WN_DISP_ALARM;
	else
		dispflags &= ~WN_DISP_ALARM;


	/*
	 * Set current pixmap according to the interface info.
	 */
	switch (ifp->status) {
	case IF_UNKNOWN:
		if (!options[OPT_KEEP].used) {
			dispflags |= WN_DISP_WARN;
			background = backlight_err;
		} else {
			dispflags |= WN_DISP_WARN;
			background = backlight_down_off;
			if (dispflags & WN_DISP_BACKLIT)
				background = backlight_down_on;
		}
		break;

	case IF_DOWN:
		dispflags |= WN_DISP_WARN;
		background = backlight_down;
		if (options[OPT_KEEP].used) {
			background = backlight_down_off;
			if (dispflags & WN_DISP_BACKLIT)
				background = backlight_down_on;
		}
		break;

	case IF_UP:
		dispflags &= ~WN_DISP_WARN;
		if (dispflags & WN_DISP_BACKLIT)
			background = backlight_on;
		break;
	}

	/*
	 * If the display flags have changed, then we have to do a full
	 * redraw no matter what.
	 */
	if (dispflags != odispflags)
		flags = WN_DRAWALL;

	/*
	 * Copy the current background to pixmap so we can modify it
	 * according to `flags'.
	 */
	WN_XCopyArea(background, pixbuf, WN_DA_WIDTH, WN_DA_HEIGHT, 0, 0);

	/*
	 * If the interface is up, draw the throughput and activity graph.
	 */
	if (ifp->status == IF_UP) {
		if (flags & WN_DRAWBPS) {
			if (dispflags & WN_DISP_INBYTES)
				draw_bps(ifp->bps, pixbuf);
			else
				draw_bps(ifp->bps * 8, pixbuf);
		}

		if (flags & WN_DRAWGRAPH)
			draw_graph(ifp->graph, pixbuf);
	}

	if ((dispflags & WN_DISP_IFNAME) && (flags & WN_DRAWIFNAME))
		draw_ifname(ifp->name, pixbuf);

	/*
	 * If WN_DRAWALL is set, then just copy the whole image.
	 * Otherwise, copy back just the requested pieces.
	 */
	if (flags == WN_DRAWALL) {
		WN_XCopyArea(pixbuf, DAWindow, WN_DA_WIDTH, WN_DA_HEIGHT, 0, 0);
		return;
	}

	if (flags & WN_DRAWBPS) {
		WN_XCopyArea(pixbuf, DAWindow, WN_BPS_WIDTH, WN_BPS_HEIGHT,
		    WN_BPS_XOFF, WN_BPS_YOFF);
	}

	if (flags & WN_DRAWGRAPH) {
		WN_XCopyArea(pixbuf, DAWindow, WN_GR_WIDTH, WN_GR_HEIGHT,
		    WN_GR_XOFF, WN_GR_YOFF);
	}

#ifdef	WN_LOOK_HAS_IFNAME
	if ((dispflags & WN_DISP_IFNAME) && (flags & WN_DRAWIFNAME)) {
		WN_XCopyArea(pixbuf, DAWindow, WN_IFN_WIDTH, WN_IFN_HEIGHT,
		    WN_IFN_XOFF, WN_IFN_YOFF);
	}
#endif
}

/*
 * Draw the interface name, using the passed in value.
 */
static void
draw_ifname(const char *ifname, Pixmap pixbuf)
{
#ifdef	WN_LOOK_HAS_IFNAME
	unsigned int syoff = WN_FONT_YOFF;
	unsigned int sxoff;
	unsigned int dxoff;
	unsigned int ifnamelen = strlen(ifname);
	unsigned int ifwidth;
	unsigned int i;

	if (dispflags & WN_DISP_BACKLIT)
		syoff = WN_FONT_YOFF + WN_FONT_HEIGHT;

	if ((!options[OPT_KEEP].used) && (dispflags & WN_DISP_WARN))
		syoff = WN_FONT_YOFF + (2 * WN_FONT_HEIGHT);

	/*
	 * If the ifname is too long to display, then chop off some letters.
	 */
	while ((ifnamelen * (WN_FONT_WIDTH + WN_IFN_SPACE)) > WN_IFN_WIDTH)
		ifnamelen--;

	/*
	 * Set dxoff so that the ifname is properly centered.
	 */
	ifwidth = ifnamelen * (WN_FONT_WIDTH + WN_IFN_SPACE);
	dxoff = (WN_IFN_WIDTH / 2) - (ifwidth / 2) + WN_IFN_XOFF;

	for (i = 0; i < ifnamelen; i++) {
		XCopyArea(DADisplay, font, pixbuf, DAGC, WN_FONT_SPCXOFF,
		    syoff, WN_IFN_SPACE, WN_FONT_HEIGHT, dxoff, WN_IFN_YOFF);
		dxoff += WN_IFN_SPACE;

		if (isalpha(ifname[i])) {
			sxoff = WN_FONT_LETXOFF;
			if (isupper(ifname[i]))
				sxoff += (ifname[i] - 'A') * WN_FONT_WIDTH;
			else
				sxoff += (ifname[i] - 'a') * WN_FONT_WIDTH;
		} else if (isdigit(ifname[i])) {
			sxoff = WN_FONT_DIGXOFF;
			sxoff += (ifname[i] - '0') * WN_FONT_WIDTH;
		} else {
			sxoff = WN_FONT_ERRXOFF;
		}

		XCopyArea(DADisplay, font, pixbuf, DAGC, sxoff, syoff,
		    WN_FONT_WIDTH, WN_FONT_HEIGHT, dxoff, WN_IFN_YOFF);
		dxoff += WN_FONT_WIDTH;
	}

	XCopyArea(DADisplay, font, pixbuf, DAGC, WN_FONT_SPCXOFF, syoff,
	    WN_IFN_SPACE, WN_FONT_HEIGHT, dxoff, WN_IFN_YOFF);
#endif
}

/*
 * Draw the bps component, using the passed in `bps' value.
 */
static void
draw_bps(ulonglong_t bps, Pixmap pixbuf)
{
	unsigned int	tens = 0;
	unsigned int	decplace = 0;
	int		digit;

	if (bps > 10)
		tens++;
	if (bps > 100)
		tens++;
	while (bps >= 1000) {
		tens++;
		decplace = WN_MODINC(decplace, 3);
		bps /= 10;
	}

	/*
	 * Ceiling the maximum throughput at 999 gbps for now.
	 */
	if (tens > 11) {
		tens = 11;
		bps = 999;
		decplace = 0;
	}

	draw_speed(tens, pixbuf);

	if (bps == 0) {
		draw_digit(0, 2, pixbuf);
		return;
	}

	if (decplace > 0 || tens < 3)
		draw_decimal(decplace, pixbuf);

	for (digit = 2; digit >= 0; digit--, bps /= 10)
		draw_digit(bps % 10, digit, pixbuf);
}

/*
 * Draw the digit named by `digit' at decimal place `place'.
 */
static void
draw_digit(unsigned int digit, unsigned int place, Pixmap pixbuf)
{
	unsigned int syoff = WN_DIG_SYOFF;

	if (dispflags & WN_DISP_BACKLIT)
		syoff += WN_DIG_HEIGHT;

	XCopyArea(DADisplay, parts, pixbuf, DAGC,
	    WN_DIG_SXOFF + (digit * WN_DIG_WIDTH), syoff,
	    WN_DIG_WIDTH, WN_DIG_HEIGHT,
	    WN_DIG_DXOFF + (place * WN_DIG_SPACE), WN_DIG_DYOFF);
}

/*
 * Draw a decimal point between "at" the decimal place `place'.
 */
static void
draw_decimal(unsigned int place, Pixmap pixbuf)
{
	XCopyArea(DADisplay, parts, pixbuf, DAGC,
	    WN_DEC_SXOFF, WN_DEC_SYOFF,
	    WN_DEC_WIDTH, WN_DEC_HEIGHT,
	    WN_DEC_DXOFF + (place * WN_DEC_SPACE), WN_DEC_DYOFF);
}

/*
 * Draw the letter representing the current speed designation.
 */
static void
draw_speed(unsigned int tens, Pixmap pixbuf)
{
	unsigned int speed = (tens < 3) ? 0 : ((tens - 3) / 3);
	unsigned int sxoff = WN_SPD_SXOFF;

	if (dispflags & WN_DISP_BACKLIT)
		sxoff += WN_SPD_WIDTH;

	XCopyArea(DADisplay, parts, pixbuf, DAGC,
	    sxoff, WN_SPD_SYOFF + (speed * WN_SPD_HEIGHT),
	    WN_SPD_WIDTH, WN_SPD_HEIGHT,
	    WN_SPD_DXOFF, WN_SPD_DYOFF + (speed * WN_SPD_SPACE));
}

/*
 * Rescale the ifgraph_t pointed to by `graph'.
 */
static void
rescale_graph(ifgraph_t *graph)
{
	unsigned int	col;
	ulonglong_t	maxbytes = 0, bytes = 0;
	ulonglong_t	scale = graph->bps2bar;

	/*
	 * First, find the biggest value...
	 */
	for (col = 0; col < WN_GR_COLS; col++) {
		bytes = graph->stats[col].txbytes + graph->stats[col].rxbytes;
		if (bytes >= maxbytes) {
			maxbytes = bytes;
			graph->maxcol = col;
		}
	}

	if ((maxbytes / scale) >= (WN_COL_HEIGHT - 1)) {
		while ((maxbytes / scale) >= (WN_COL_HEIGHT - 1))
			scale *= 2;
	} else {
		/*
		 * See if we need to scale down.
		 */
		while (scale > WN_DEF_BPS2BAR) {
			if ((maxbytes / (scale / 2)) >= (WN_COL_HEIGHT - 1))
				break;
			scale /= 2;
		}
	}

	if (scale != graph->bps2bar) {
		for (col = 0; col < WN_GR_COLS; col++) {
			graph->tbars[col] = graph->stats[col].txbytes / scale;
			graph->rbars[col] = graph->stats[col].rxbytes / scale;
		}
		graph->bps2bar = scale;
	}
}

/*
* Draw the network activity graph using the interface graph statistics
 * pointed to by `graph'.
 */
static void
draw_graph(ifgraph_t *graph, Pixmap pixbuf)
{
	int		c;
	unsigned int	sxoff;
	unsigned int	col = graph->col;
	ulonglong_t	*tbars = graph->tbars;
	ulonglong_t	*rbars = graph->rbars;

	tbars[col] = graph->stats[col].txbytes / graph->bps2bar;
	rbars[col] = graph->stats[col].rxbytes / graph->bps2bar;

	/*
	 * If the column we just collected either is too large to fit
	 * in the graph or is replacing a column that was previously
	 * the biggest value, then rescale.
	 */
	if (((tbars[col] + rbars[col]) >= (WN_COL_HEIGHT - 1)) ||
	    (col == graph->maxcol))
		rescale_graph(graph);

	sxoff = WN_COL_SXOFF;
	if (dispflags & WN_DISP_BACKLIT)
		sxoff += WN_COL_WIDTH;

	for (c = WN_GR_COLS - 1; c >= 0; c--) {
		XCopyArea(DADisplay, parts, pixbuf, DAGC,
		    sxoff, WN_COL_SYOFF + WN_COL_HEIGHT - tbars[col],
		    WN_COL_WIDTH, tbars[col],
		    WN_COL_DXOFF + (c * WN_COL_SPACE),
		    WN_COL_DYOFF + WN_COL_HEIGHT - tbars[col]);

		XCopyArea(DADisplay, parts, pixbuf, DAGC,
		    sxoff, WN_COL_SYOFF, WN_COL_WIDTH, rbars[col],
		    WN_COL_DXOFF + (c * WN_COL_SPACE), WN_COL_DYOFF);

		col = WN_MODDEC(col, WN_GR_COLS);
	}
}

/*
 * Create an ifinfo_t for an interface named `ifname'.  If it returns,
 * the pointer returned is guaranteed to be valid.
 */
static ifinfo_t *
ifinfo_create(const char *ifname)
{
	ifinfo_t *ifp;

	ifp = calloc(1, sizeof (ifinfo_t));
	if (ifp == NULL)
		die("cannot allocate interface information structure");

	ifp->name = strdup(ifname);
	if (ifp->name == NULL)
		die("cannot allocate interface name");

	ifp->graph = calloc(1, sizeof (ifgraph_t));
	if (ifp->graph == NULL)
		die("cannot allocate interface statistics graph");
	ifp->graph->bps2bar = WN_DEF_BPS2BAR;

	ifp->statep = if_statinit();
	if (ifp->statep == NULL)
		die("cannot initialize interface statistics");

	return (ifp);
}

/*
 * Destroy the ifinfo_t pointed to by `ifp'.
 */
static void
ifinfo_destroy(ifinfo_t *ifp)
{
	if_statfini(ifp->statep);
	free(ifp->name);
	free(ifp->graph);
	free(ifp);
}

/*
 * Check if the named interface is functioning.
 */
static ifstatus_t
if_status(int fd, const char *ifname)
{
	int flags = if_flags(fd, ifname);

	if (flags == -1)
		return (IF_UNKNOWN);

	return ((flags & IFF_UP) != 0 ? IF_UP : IF_DOWN);
}

/*
 * Get an interface's current flags, or -1 if the cannot be retrieved.
 */
static int
if_flags(int fd, const char *ifname)
{
	struct ifreq ifr;

	(void) strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';

	if (ioctl(fd, SIOCGIFFLAGS, &ifr) == -1)
		return (-1);

	return (ifr.ifr_flags);
}

/*
 * Return a pointer to the next ifreq structure after `ifrp'.  See the rant
 * in if_next() for more on this misery.
 */
static struct ifreq *
ifr_next(struct ifreq *ifrp)
{
	unsigned int len;

#ifdef	HAVE_SOCKADDR_SA_LEN
	len = ifrp->ifr_addr.sa_len;
	if (len < sizeof (struct sockaddr))
		len = sizeof (struct sockaddr);
#else
	switch (ifrp->ifr_addr.sa_family) {
#ifdef	HAVE_IPV6
	case AF_INET6:
		len = sizeof (struct sockaddr_in6);
		break;
#endif
	case AF_INET:
	default:
		len = sizeof (struct sockaddr);
		break;
	}
#endif
	len += sizeof (ifrp->ifr_name);
	return ((struct ifreq *)((caddr_t)ifrp + len));
}

/*
 * Fetch the name of the "next" interface in the interface list.  If the
 * passed in interface name is empty, then return the first interface name
 * in the list that isn't loopback (unless loopback is the only interface).
 * If the next interface name cannot be determined, then 0 is returned.
 */
static int
if_next(int fd, const char *ifname, char *nextifname)
{
	struct ifconf	ifc;
	struct ifreq	*ifrp, *matchifrp = NULL, *loifrp;
	void		*startp, *endp;
	int		flags;
	int		ifcount;
	int		prevlen;

	/*
	 * Fetch the number of interfaces on the system.
	 */
#ifdef	SIOCGIFNUM
	if (ioctl(fd, SIOCGIFNUM, &ifcount) == -1)
		return (0);
	ifc.ifc_len = ifcount * sizeof (struct ifreq);
#else
	/*
	 * Unfortunately, different flavors of Unix have different semantics
	 * for when the buffer is too small.  Specifically, some (like Linux)
	 * will set ifc_len to be the size that would be needed to return all
	 * of the interface information, whereas others (like FreeBSD) will
	 * set ifc_len to be the amount of ifc_buf that was copied out to
	 * userland.  Thus, we have to do this weird dance to support both.
	 */
	ifc.ifc_len = 0;
	ifc.ifc_buf = NULL;
	do {
		prevlen = ifc.ifc_len;
		ifc.ifc_len += 1024;
		ifc.ifc_buf = realloc(ifc.ifc_buf, ifc.ifc_len);
		if (ifc.ifc_buf == NULL) {
			warn("cannot allocate interface information");
			return (0);
		}
		if (ioctl(fd, SIOCGIFCONF, &ifc) == -1 || ifc.ifc_len <= 0) {
			free(ifc.ifc_buf);
			return (0);
		}
	} while (ifc.ifc_len > prevlen);

	free(ifc.ifc_buf);
#endif
	/*
	 * Allocate enough ifreq entries to store the results.
	 */
	ifc.ifc_req = alloca(ifc.ifc_len);
	if (ioctl(fd, SIOCGIFCONF, &ifc) == -1)
		return (0);

	/*
	 * *@@!*(&#$: A special thanks to the BSD team, for breaking the
	 * SIOCGIFCONF interface when they introduced IPv6 support, by
	 * making ifreqs vary in size.  To make matters worse, most Unices
	 * do not support sa_len, so we don't even have a portable way to
	 * step through the ifreqs.  Why BSD didn't just introduce a new
	 * ioctl to retrieve the extended ifreqs is beyond me.
	 */
	startp = ifc.ifc_buf;
	endp = ifc.ifc_buf + ifc.ifc_len;

	/*
	 * Loop through all the interface names.  If we were given an
	 * interface name, then attempt to return the next interface
	 * "after" it (looping around to the first interface, if need be).
	 * If we weren't given an interface name, or if we cannot find the
	 * requested interface name, then return the first non-loopback
	 * interface.
	 */
	for (ifrp = startp; (void *)&ifrp[1] <= endp; ifrp = ifr_next(ifrp)) {
		if (ifname != NULL && strcmp(ifrp->ifr_name, ifname) == 0) {
			/*
			 * Each interface ends up in the ifreq list once
			 * for each address family it's configured with.
			 * If these entries end up being consecutive, that
			 * will screw up our iterator since we iterate
			 * by name.  As such, make sure we skip to the
			 * final instance of the name we find.
			 */
			matchifrp = ifrp;
			do {
				matchifrp = ifr_next(matchifrp);
				if ((void *)&matchifrp[1] > endp)
					matchifrp = startp;
			} while (matchifrp != ifrp &&
			    strcmp(ifrp->ifr_name, matchifrp->ifr_name) == 0);
			break;
		}

		/*
		 * Just in case we never find a non-loopback interface,
		 * keep track of the first loopback interface we find.
		 */
		flags = if_flags(fd, ifrp->ifr_name);
		if (flags != -1 && (flags & IFF_LOOPBACK)) {
			if (loifrp == NULL)
				loifrp = ifrp;
			continue;
		}

		/*
		 * If this is the first non-loopback interface we've found,
		 * then store it in `matchifrp' in case we don't find a
		 * better match.
		 */
		if (matchifrp == NULL) {
			matchifrp = ifrp;
			if (ifname == NULL)
				break;
		}
	}

	if (matchifrp == NULL)
		matchifrp = loifrp;

	if (matchifrp != NULL) {
		(void) strncpy(nextifname, matchifrp->ifr_name, IFNAMSIZ);
		nextifname[IFNAMSIZ - 1] = '\0';
		return (1);
	}
	return (0);
}

/*
 * Make pixmaps from the raw XPM data.
 */
static int
xpm2pixmap(void)
{
	struct {
		char	**xpm;
		Pixmap	*pixmap;
	} xpm2pixmap[] = {
		{ backlight_on_xpm,	&backlight_on	},
		{ backlight_off_xpm,	&backlight_off	},
		{ backlight_err_xpm,	&backlight_err	},
		{ backlight_down_xpm,	&backlight_down	},
		{ parts_xpm,		&parts		},
		{ font_xpm,		&font 		},
		{ backlight_down_on_xpm, &backlight_down_on},
		{ backlight_down_off_xpm, &backlight_down_off},
		{ NULL }
	};
	unsigned int h, w, i;
	XpmColorSymbol colors[] = { {"Back0", NULL, 0}, {"Back1", NULL, 0} };
	XpmAttributes xpmAttr;

	xpmAttr.valuemask = XpmCloseness;
	xpmAttr.closeness = 40000;

	if (options[OPT_LIGHTCOLOR].used) {
		colors[0].pixel = DAGetColor(lightcolor);
		colors[1].pixel = getblendedcolor(lightcolor, -24);
		xpmAttr.numsymbols = 2;
		xpmAttr.colorsymbols = colors;
		xpmAttr.valuemask |= XpmColorSymbols;
	}

	for (i = 0; xpm2pixmap[i].xpm != NULL; i++) {
		if (XpmCreatePixmapFromData(DADisplay, DAWindow,
		    xpm2pixmap[i].xpm, xpm2pixmap[i].pixmap, NULL,
		    &xpmAttr) != 0)
			return (0);
	}

	return (1);
}

/*
 * Set the shape of the the dockapp.
 */
static void
setshape(void)
{
	Pixmap mask, pixmap;
	short unsigned int h, w;

	if (DAMakePixmapFromData(backlight_off_xpm, &pixmap, &mask, &h, &w)) {
		DASetShape(mask);
		XFreePixmap(DADisplay, mask);
		XFreePixmap(DADisplay, pixmap);
	}
}

/*
 * Initialize a smoothing table containing `niter + 1' elements.
 */
static double *
smoothtable_init(unsigned int niter)
{
	unsigned int iter;
	unsigned int nslice = 0;
	double *smoothtable;

	smoothtable = calloc(niter + 1, sizeof (double));
	if (smoothtable == NULL)
		return (NULL);

	for (iter = 0; iter <= niter; iter++)
		nslice += (iter * (iter + 1));

	for (iter = 0; iter <= niter; iter++)
		smoothtable[iter] = (double)(iter * (iter + 1)) / nslice;

	return (smoothtable);
}

/*
 * Initialize the smoothing time table, using the general smoothing table
 * named by `smoothtable', the total amount of time per update interval (in
 * milliseconds, and the total number of smoothing iterations per update
 * interval.
 */
static unsigned int *
timetable_init(double smoothtable[], unsigned int interval, unsigned int niter)
{
	unsigned int *timetable;
	unsigned int iter;

	timetable = calloc(niter + 1, sizeof (unsigned int));
	if (timetable == NULL)
		return (NULL);

	timetable[0] = interval;
	for (iter = 1; iter < niter; iter++)
		timetable[iter] = timetable[iter - 1] -
		    (interval * smoothtable[iter]);
	timetable[niter] = 0;

	return (timetable);
}

/*
 * Given specified `red', `green', and `blue' values, return the closest
 * approximation to that value in the color table.  Only needed for
 * severely constrained visual modes.
 */
static unsigned long
approxpixel(unsigned long red, unsigned long green, unsigned long blue)
{
	XColor *colorcells;
	XColor approxcolor;
	int i, besti;
	unsigned long bestapprox = ULONG_MAX;
	unsigned long approx, diffr, diffg, diffb;
	unsigned int ncell;

	ncell = DisplayCells(DADisplay, DefaultScreen(DADisplay));
	colorcells = malloc(ncell * sizeof (XColor));
	if (colorcells == NULL)
		die("cannot allocate color cells");

	/* get all colors from default colorcells */
	for (i = 0; i < ncell; i++)
		colorcells[i].pixel = i;

	XQueryColors(DADisplay, DefaultColormap(DADisplay,
	    DefaultScreen(DADisplay)), colorcells, ncell);

	/* cruise colorcells, find the closest approximate color */
	for (i = 0; i < ncell; i++) {
		diffr = (red - colorcells[i].red) >> 8;
		diffg = (green - colorcells[i].green) >> 8;
		diffr = (blue - colorcells[i].blue) >> 8;

		approx = diffr * diffr + diffg * diffg + diffb * diffb;
		if (approx < bestapprox) {
			bestapprox = approx;
			besti = i;
		}
	}

	if (bestapprox == ULONG_MAX) {
		warn("cannot approximate color #%lu/%lu/%lu, using black\n");
		free(colorcells);
		return (BlackPixel(DADisplay, DefaultScreen(DADisplay)));
	}

	approxcolor.red = colorcells[besti].red;
	approxcolor.blue = colorcells[besti].blue;
	approxcolor.green = colorcells[besti].green;
	colorcells[besti].flags = DoRed | DoGreen | DoBlue;

	free(colorcells);

	if (!XAllocColor(DADisplay, DefaultColormap(DADisplay,
	    DefaultScreen(DADisplay)), &approxcolor)) {
		warn("cannot approximate color #%lu/%lu/%lu, using black\n");
		return (BlackPixel(DADisplay, DefaultScreen(DADisplay)));
	}

	return (approxcolor.pixel);
}

/*
 * Given a pixel colorvalue 'color', blend it linearly by 'blend' if
 * possible.
 */
static unsigned short
blendcolor(int color, int blend)
{
	if (color + blend > 0xffff)
		return (0xffff);
	if (color + blend < 0)
		return (0);
	return (color + blend);
}

/*
 * Given the color name specified by `colorname', blend the red, green and
 * blue values associated with it linearly by `blend' amount and return the
 * new pixel value.
 */
static unsigned long
getblendedcolor(const char *colorname, int blend)
{
	XColor color;
	Visual *visual = DefaultVisual(DADisplay, DefaultScreen(DADisplay));

	if (!XParseColor(DADisplay, DefaultColormap(DADisplay,
	    DefaultScreen(DADisplay)), colorname, &color))
		die("cannot parse color %s\n", colorname);

	color.red = blendcolor(color.red, blend * 255);
	color.blue = blendcolor(color.blue, blend * 255);
	color.green = blendcolor(color.green, blend * 255);

	if (visual->class == PseudoColor || visual->class == GrayScale)
		return (approxpixel(color.red, color.green, color.blue));

	if (!XAllocColor(DADisplay, DefaultColormap(DADisplay,
	    DefaultScreen(DADisplay)), &color))
		return (BlackPixel(DADisplay, DefaultScreen(DADisplay)));

	return (color.pixel);
}
