#
#  Incremental Snapshot
#
#  Copyright (C) 2006, Joshua D. Henderson <www.digitalpeer.com>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#

AC_INIT(isnapshot,0.1beta)
AM_INIT_AUTOMAKE

AM_MAINTAINER_MODE

CFLAGS=" $CFLAGS"
CXXFLAGS=" $CXXFLAGS"
LDFLAGS=" $LDFLAGS"

AC_ARG_ENABLE(debug,   [  --enable-debug         compile with debugging support],,enable_debug=no)

if test "$enable_debug" = yes ; then
	CFLAGS=" $CFLAGS -DDEBUG"
	CXXFLAGS=" $CXXFLAGS -DDEBUG"
fi

dnl Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AM_PROG_CC_STDC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_RANLIB

AC_OUTPUT([Makefile src/Makefile])
