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
# SCCS  "@(#)Makefile.am	1.3	02/12/12 meem"
#
# Process this file with automake to produce Makefile.in
#

SUBDIRS = src

EXTRA_DIST = wmnetload.lsm.in xpm

wmnetload.lsm: FRC
	size=`wc $(distdir).tar.gz | awk '{ printf("%2.2fK", $$3 / 1024) }'`; \
	sed -e "s/VERSION/"$(VERSION)"/" \
		-e "s/DATE/`date +%d%b%Y|tr a-z A-Z`/"\
		-e "s/SIZE/$$size/"\
		wmnetload.lsm.in > wmnetload-$(VERSION).lsm

FRC:
