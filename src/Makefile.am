#
# Copyright (c) 2002 Peter Memishian (meem) <meem@gnu.org>
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
# SCCS  "@(#)Makefile.am	1.5	03/02/18 meem"
#
# Process this file with automake to produce Makefile.in
#

bin_PROGRAMS		= wmnetload
wmnetload_SOURCES	= wmnetload.c ifstat.h ifstat_@OS@.c utils.h utils.c
EXTRA_wmnetload_SOURCES	= ifstat_linux.c ifstat_netbsd.c ifstat_solaris.c \
			  ifstat_freebsd.c

LDFLAGS			= @RPATH@

LDADD	 = @LIBRARY_SEARCH_PATH@ @XLFLAGS@ @XLIBS@ -ldockapp -lXpm -lm
CPPFLAGS = @CPPFLAGS@ @XCFLAGS@ -DVERSION=\"@VERSION@\" 
INCLUDES = @HEADER_SEARCH_PATH@ -I$(top_srcdir)/xpm/@WN_LOOK@
