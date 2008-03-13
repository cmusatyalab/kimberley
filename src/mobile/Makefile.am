bin_PROGRAMS = display_launcher mobile_launcher
bin_SCRIPTS = display_setup

BUILT_SOURCES = \
	rpc_mobile_launcher_clnt.c rpc_mobile_launcher_svc.c \
	rpc_mobile_launcher_xdr.c rpc_mobile_launcher.x rpc_mobile_launcher.h \
	dcm_dbus_app_glue.h

CLEANFILES = $(BUILT_SOURCES)

display_launcher_SOURCES = display_launcher.c display_launcher.h \
	mobile_launcher_server.c rpc_mobile_launcher.x.in dcm.xml \
	common.c common.h \
	rpc_mobile_launcher_svc.c rpc_mobile_launcher_xdr.c rpc_mobile_launcher.h


mobile_launcher_SOURCES = mobile_launcher.c \
	rpc_mobile_launcher.x.in dcm.xml \
	common.c common.h \
	rpc_mobile_launcher_clnt.c rpc_mobile_launcher_xdr.c rpc_mobile_launcher.h

rpc_%_xdr.c: rpc_%.x
	$(RM) $@
	rpcgen -M -N -C -c -o $@ $<

rpc_%.h: rpc_%.x
	$(RM) $@
	rpcgen -M -N -C -h -o $@ $<

rpc_%_svc.c: rpc_%.x
	$(RM) $@
	rpcgen -M -N -C -m -o $@ $<

rpc_%_clnt.c: rpc_%.x
	$(RM) $@
	rpcgen -M -N -C -l -o $@ $<

%.x: %.x.in
	cp $< $@

dcm_dbus_app_glue.h: dcm.xml
	$(RM) $@
	dbus-binding-tool --mode=glib-client --prefix=dcm $< > $@
