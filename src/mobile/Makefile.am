bin_PROGRAMS = display_launcher mobile_launcher
bin_SCRIPTS = display_setup

CPPFLAGS = $(DBUS_GLIB_CFLAGS) ${MINIRPC_CFLAGS}
LDADD = $(DBUS_GLIB_LIBS) ${MINIRPC_LIBS}

MINIRPC_FILES = rpc_mobile_launcher.mx

display_launcher_SOURCES = display_launcher.c display_launcher.h \
	mobile_launcher_server.c common.c common.h \
	$(MINIRPC_FILES) kcm.xml


mobile_launcher_SOURCES = mobile_launcher.c common.c common.h \
	$(MINIRPC_FILES) kcm.xml

MINIRPC_GENERATED = \
	${MINIRPC_FILES:.mx=_client.h}  ${MINIRPC_FILES:.mx=_server.h} \
	${MINIRPC_FILES:.mx=_minirpc.c}  ${MINIRPC_FILES:.mx=_minirpc.h} \
	${MINIRPC_FILES:.mx=_xdr.c}  ${MINIRPC_FILES:.mx=_xdr.h}

nodist_display_launcher_SOURCES = ${MINIRPC_GENERATED}
nodist_mobile_launcher_SOURCES = ${MINIRPC_GENERATED}

BUILT_SOURCES = ${MINIRPC_GENERATED} kcm_dbus_app_glue.h
CLEANFILES = ${BUILT_SOURCES}

%_client.h %_server.h %_minirpc.c %_minirpc.h %_xdr.c %_xdr.h: %.mx
	$(MINIRPCGEN) $<

kcm_dbus_app_glue.h: kcm.xml
	$(RM) $@
	dbus-binding-tool --mode=glib-client --prefix=dcm $< > $@
