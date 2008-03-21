#  The OpenDiamond Platform for Interactive Search
#  Version 3 
#
#  Copyright (c) 2007 Carnegie Mellon University
#  All rights reserved.
#
#  This software is distributed under the terms of the Eclipse Public
#  License, Version 1.0 which can be found in the file named LICENSE.
#  ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES
#  RECIPIENT'S ACCEPTANCE OF THIS AGREEMENT
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


# glib
PKG_CHECK_MODULES(GLIB2, [glib-2.0, gthread-2.0])
AC_SUBST(GLIB2_CFLAGS)
AC_SUBST(GLIB2_LIBS)

# D-Bus
PKG_CHECK_MODULES(DBUS, [dbus-1])
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

# D-Bus glib bindings
PKG_CHECK_MODULES(DBUS_GLIB, [dbus-glib-1])
AC_SUBST(DBUS_GLIB_CFLAGS)
AC_SUBST(DBUS_GLIB_LIBS)

# libs
AC_SEARCH_LIBS([pthread_create],
	[pthread],, AC_MSG_FAILURE([cannot find pthread_create function]))
AC_SEARCH_LIBS([OpenSSL_add_all_digests],
	[ssl],, AC_MSG_FAILURE([cannot find ssl function]))
AC_SEARCH_LIBS([dbus_bus_get],
    [dbus-1],, AC_MSG_FAILURE([cannot find dbus_bus_get function]))
AC_SEARCH_LIBS([dbus_g_bus_get],
    [dbus-glib-1],, AC_MSG_FAILURE([cannot find dbus_g_bus_get function]))
AC_SEARCH_LIBS([ceilf],
    [m],, AC_MSG_FAILURE([cannot find ceilf function]))

# some options and includes
AC_SUBST(AM_CPPFLAGS, ['-D_REENTRANT -I$(top_srcdir)/common -I$(top_srcdir)/lib/libauth -I$(top_srcdir)/lib/libdconfig -I$(top_srcdir)/lib/libdctl -I$(top_srcdir)/lib/libfilter -I$(top_srcdir)/lib/libfilterexec -I$(top_srcdir)/lib/liblog -I$(top_srcdir)/lib/libodisk -I$(top_srcdir)/lib/libsearchlet -I$(top_srcdir)/lib/libtools -I$(top_srcdir)/lib/transport/socket/common -I$(top_srcdir)/lib/transport/socket/hoststub -I$(top_srcdir)/lib/transport/socket/storagestub'])

AC_SUBST(AM_CFLAGS, ['-Wall -Wextra -Werror-implicit-function-declaration -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wnested-externs $(GLIB2_CFLAGS) $(DBUS_CFLAGS) $(DBUS_GLIB_CFLAGS)'])


# done
AC_CONFIG_FILES([
Makefile
src/Makefile
src/mobile/Makefile
src/kimberlize/Makefile
])
AC_OUTPUT
