#
#  Kimberley
#
#  Copyright (c) 2008-2009 Carnegie Mellon University
#  All rights reserved.
#
#  Kimberley is free software: you can redistribute it and/or modify
#  it under the terms of version 2 of the GNU General Public License
#  as published by the Free Software Foundation.
#
#  Kimberley is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with Kimberley. If not, see <http://www.gnu.org/licenses/>.
#

AC_INIT([kimberlize], [1.0.0], [diamond@cs.cmu.edu])
AC_PREREQ([2.59])
AC_CONFIG_SRCDIR([config.h.in])
AM_INIT_AUTOMAKE([foreign 1.8.5])
AC_CONFIG_HEADERS([config.h])

AC_PROG_CC
AM_PROG_LEX

AC_DISABLE_STATIC
AC_PROG_LIBTOOL

AC_PATH_PROG(RPCGEN, rpcgen)

# D-Bus glib bindings
PKG_CHECK_MODULES(DBUS_GLIB, [dbus-glib-1])
AC_SUBST(DBUS_GLIB_CFLAGS)
AC_SUBST(DBUS_GLIB_LIBS)

# libs
AC_SEARCH_LIBS([pthread_create],
	[pthread],, AC_MSG_FAILURE([cannot find pthread_create function]))

# some options and includes
AC_SUBST(AM_CPPFLAGS, ['-D_REENTRANT'])
AC_SUBST(AM_CFLAGS, ['-Wall -Wextra -Werror-implicit-function-declaration -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wnested-externs'])


# done
AC_CONFIG_FILES([
Makefile
src/Makefile
src/mobile/Makefile
src/kimberlize/Makefile
])
AC_OUTPUT
