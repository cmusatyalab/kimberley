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

# D-Bus glib bindings
PKG_CHECK_MODULES(DBUS_GLIB, [dbus-glib-1])
AC_SUBST(DBUS_GLIB_CFLAGS)
AC_SUBST(DBUS_GLIB_LIBS)

# libs
AC_SEARCH_LIBS([pthread_create],
	[pthread],, AC_MSG_FAILURE([cannot find pthread_create function]))
AC_SEARCH_LIBS([OpenSSL_add_all_digests],
	[ssl],, AC_MSG_FAILURE([cannot find ssl function]))

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
