#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <glib.h>

#include <minirpc/minirpc.h>
#include "rpc_mobile_launcher_server.h"

#include "kcm_dbus_app_glue.h"
#include "display_launcher.h"
#include "common.h"

static struct mrpc_conn_set *mrpc_cset;

static DBusGConnection *dbus_conn;

int
create_kcm_service(char *name, char *svc)
{
    DBusGProxy *dbus_proxy = NULL;
    GError *gerr = NULL;
    int ret = 0, i;
    guint gport;
    gchar **interface_strs = NULL;
    gint interface = -1;

    if(name == NULL) {
      fprintf(stderr, "(display-launcher) bad args to create_kcm_service\n");
      return -1;
    }

    g_type_init();

    fprintf(stderr, "(display-launcher) connecting to DBus..\n");

    if(dbus_conn == NULL) {
      dbus_conn = dbus_g_bus_get(DBUS_BUS_SESSION, &gerr);
      if(dbus_conn == NULL) {
	if(gerr)
	  g_warning("Unable to connect to DBus: %sn", gerr->message);
	ret = -1;
	goto cleanup;
      }
    }

    fprintf(stderr, "(display-launcher) creating DBus proxy..\n");

    /* This won't trigger activation! */
    dbus_proxy = dbus_g_proxy_new_for_name(dbus_conn,
					   KCM_DBUS_SERVICE_NAME,
					   KCM_DBUS_SERVICE_PATH,
					   KCM_DBUS_SERVICE_NAME);

    fprintf(stderr, "(display-launcher) DBus proxy calling into "
	    "KCM (name=%s, port=%s)..\n", name, svc);

    fprintf(stderr, "(display-launcher) dbus proxy making call (sense)..\n");

    /* The method call will trigger activation. */
    if(!edu_cmu_cs_kimberley_kcm_sense(dbus_proxy, &interface_strs, &gerr)) {
      /* Method failed, the GError is set, let's warn everyone */
      g_warning("(display-launcher) kcm->sense() method failed: %s",
		gerr->message);
      g_error_free(gerr);
      ret = -1;
      goto cleanup;
    }

    if(interface_strs != NULL) {
      fprintf(stderr, "(display-launcher) Found some interfaces:\n");
      for(i=0; interface_strs[i] != NULL; i++)
	fprintf(stderr, "\t%d: %s\n", i, interface_strs[i]);
      fprintf(stderr, "\n");
    }

    fprintf(stderr, "(display-launcher) dbus proxy making call (publish)..\n");

    gport = atoi(svc);

    if(!edu_cmu_cs_kimberley_kcm_publish(dbus_proxy, name, interface,
					 gport, &gerr)) {
      if(gerr != NULL) {
	g_warning("server() method failed: %s", gerr->message);
	g_error_free(gerr);
      }
      ret = -1;
      goto cleanup;
    }


 cleanup:

    if(gerr) g_error_free (gerr);
    if(dbus_proxy) g_object_unref(dbus_proxy);

    /* The DBusGConnection should never be unreffed,
     * it lives once and is shared amongst the process */

    return ret;
}

static void *new_conn(void *set_data, struct mrpc_connection *conn,
		      struct sockaddr *from, socklen_t fromlen)
{
    kimberley_state_t *state;

    fprintf(stderr, "(display-launcher) Accepting KCM connection..\n");

    state = calloc(1, sizeof(kimberley_state_t));

    rpc_mobile_launcher_server_set_operations(conn, server_ops);

    return state;
}

static void dead_conn(void *conn_data, enum mrpc_disc_reason reason)
{
    kimberley_state_t *state = conn_data;
    int fd;

    fprintf(stderr, "(display-launcher) A connection was closed.\n");

    /*
     * Signal dekimberlize that the connection was lost, if it hasn't
     * been signaled yet.
     */
    fd = open("/tmp/dekimberlize_finished", O_RDWR|O_CREAT);
    close(fd);

    if (state->overlay_location &&
	remove(state->overlay_location) && errno != ENOENT)
	perror("remove overlay_location");

    if (state->encryption_key &&
	remove(state->encryption_key) && errno != ENOENT)
	perror("remove encryption_key");

    if (state->persistent_state) {
	char *tmp = malloc(strlen(state->persistent_state) + 6);

	if (remove(state->persistent_state) && errno != ENOENT)
	    perror("remove persistent_state");

	sprintf(tmp, "%s.new",state->persistent_state);
	if (remove(tmp) && errno != ENOENT)
	    perror("remove persistent_state modified");

	sprintf(tmp, "%s.diff", state->persistent_state);
	if (remove(tmp) && errno != ENOENT)
	    perror("remove persistent_state diff");
    }

    free(state->vm_name);
    free(state->patch_file);
    free(state->overlay_location);
    free(state->encryption_key);
    free(state->persistent_state);
    free(state);

    log_deinit();
}

static void catch_sigint(int sig)
{
    fprintf(stderr, "\n(display-launcher) SIGINT caught.  Cleaning up..\n");
    mrpc_listen_close(mrpc_cset);
    /* close all connections... */
    mrpc_conn_set_unref(mrpc_cset);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    char *svc = NULL;

    if(log_init() < 0) {
	fprintf(stderr, "(display-launcher) Couldn't initialize log!\n");
	exit(EXIT_FAILURE);
    }

    log_message("display launcher started up..");

    signal(SIGINT, catch_sigint);

    if (mrpc_conn_set_create(&mrpc_cset, rpc_mobile_launcher_server, NULL)) {
	fprintf(stderr, "Failed to intialize minirpc connection set\n");
	exit(EXIT_FAILURE);
    }

    if (mrpc_start_dispatch_thread(mrpc_cset)) {
	fprintf(stderr, "Failed to start minirpc dispatch thread\n");
	exit(EXIT_FAILURE);
    }

    if (mrpc_set_accept_func(mrpc_cset, new_conn) ||
	mrpc_set_disconnect_func(mrpc_cset, dead_conn)) {
	fprintf(stderr, "Failed to set minirpc callback functions\n");
	exit(EXIT_FAILURE);
    }

    if (mrpc_listen(mrpc_cset, AF_INET, NULL, &svc)) {
	fprintf(stderr, "Failed to create minirpc listening socket\n");
	exit(EXIT_FAILURE);
    }

    fprintf(stderr, "(display-launcher) bound to localhost:%s..\n", svc);

    while(1) {
	struct timeval tv = { .tv_sec = 3600 };

	fprintf(stderr, "(display-launcher) registering with KCM..\n");

	if (create_kcm_service(LAUNCHER_KCM_SERVICE_NAME, svc) < 0)
	{
	    fprintf(stderr, "(display-launcher) failed sending message to KCM. "
		    "Sleeping one second and trying again..\n");

	    tv.tv_sec = 1;
	}

	select(0, NULL, NULL, NULL, &tv);
    }
    exit(EXIT_SUCCESS);
}
