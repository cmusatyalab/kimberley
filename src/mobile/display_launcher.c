#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <glib.h>
#include <unistd.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include "dcm_dbus_app_glue.h"
#include "rpc_mobile_launcher.h"
#include "display_launcher.h"
#include "common.h"


char		service_name[MAXPATHLEN];
char 		vm_name[MAXPATHLEN];


int
create_dcm_service(char *name, unsigned short port) {
    DBusGConnection *dbus_conn;
    DBusGProxy *dbus_proxy = NULL;
    GError *gerr = NULL;
    int ret = 0;
    gchar *gname;
    guint gport;


    if(name == NULL) {
      fprintf(stderr, "(display-launcher) bad args to create_dcm_service\n");
      return -1;
    }

    g_type_init();

    fprintf(stderr, "(display-launcher) connecting to DBus..\n");

    dbus_conn = dbus_g_bus_get(DBUS_BUS_SESSION, &gerr);
    if(dbus_conn == NULL) {
      if(gerr)
        g_warning("Unable to connect to DBus: %sn", gerr->message);
      ret = -1;
      goto cleanup;
    }
    
    fprintf(stderr, "(display-launcher) creating DBus proxy..\n");
    
    /* This won't trigger activation! */
    dbus_proxy = dbus_g_proxy_new_for_name(dbus_conn,
					   DCM_DBUS_SERVICE_NAME,
					   DCM_DBUS_SERVICE_PATH,
					   DCM_DBUS_SERVICE_NAME);
    
    fprintf(stderr, "(display-launcher) DBus proxy calling into "
	    "DCM (name=%s, port=%u)..\n", name, port);
    
    /* Signal DCM that a service is ready to accept new connections.
     * The method call will trigger activation.  In other words,
     * if the DCM is not running before this call is made, it will be
     * afterwards, assuming service files are installed correctly. */

    gname = name;
    gport = port;
    if(!edu_cmu_cs_diamond_opendiamond_dcm_server(dbus_proxy, name,
						  gport, &gerr)) {
      if(gerr != NULL)
        g_warning("server() method failed: %s", gerr->message);
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


int
main(int argc, char *argv[])
{
  int                listenfd, dcm_connfd, rpc_connfd;
  struct sockaddr_in sa;
  int                err;
  unsigned short     port;

  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if(listenfd < 0) {
    perror("socket");
    return -1;
  }
  
  port = choose_random_port();
  
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  
  fprintf(stderr, "(display-launcher) binding to localhost:%u..\n", port);
  
  err = bind(listenfd, (struct sockaddr *) &sa, sizeof(sa));
  if(err) {
    perror("bind");
    return -1;
  }
  
  fprintf(stderr, "(display-launcher) local tunnel listening..\n");
  
  err = listen(listenfd, SOMAXCONN);
  if(err) {
    perror("listen");
    return -1;
  }
  
  fprintf(stderr, "(display-launcher) bringing up mobile launcher "
	  "RPC server..\n");
    
  rpc_connfd = setup_rpc_server_and_connection(MOBILELAUNCHER_PROG, 
					       MOBILELAUNCHER_VERS,
					       mobilelauncher_prog_1);

  fprintf(stderr, "(display-launcher) registering with DCM..\n");
  
  if(create_dcm_service(LAUNCHER_DCM_SERVICE_NAME, port) < 0) {
    fprintf(stderr, "(display-launcher) failed sending message to DCM..\n");
    return -1;
  }

  fprintf(stderr, "(display-launcher) Accepting DCM connection..\n");
  
  
  dcm_connfd = accept(listenfd, NULL, NULL);
  if(dcm_connfd < 0) {
    perror("accept");
    return -1;
  }
  
  fprintf(stderr, "(display-launcher) Tunneling..\n");
  
  local_tunnel(dcm_connfd, rpc_connfd);
  
  /* XXX: Shouldn't get here! */
  fprintf(stderr, "(display-launcher) Tunnel finished unexpectedly!\n");
  
  return -1;
}
