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

#include "dcm_dbus_app_glue.h"
#include "rpc_mobile_launcher.h"
#include "display_launcher.h"
#include "common.h"


volatile kimberley_state_t current_state;


int
cleanup(void) {
  int err, fd;

  /*
   * We're bringing down the connection, so no locking is really necessary,
   * so try and get one but otherwise continue on.
   */


  /*
   * Signal dekimberlize that the connection was lost, if it hasn't
   * been signaled yet.
   */

  fd = open("/tmp/dekimberlize_finished", O_RDWR|O_CREAT);
  close(fd);

  err = pthread_mutex_trylock(&current_state.mutex);
  if(err < 0)
    fprintf(stderr, "(display-launcher) pthread_mutex_lock returned "
	    "error: %d\n", err);

  if(strlen(current_state.overlay_filename) > 0)
    if(remove(current_state.overlay_filename) < 0)
      if(errno != ENOENT)
	perror("remove");

  if(strlen(current_state.persistent_state_filename) > 0)
    if(remove(current_state.persistent_state_filename) < 0)
      if(errno != ENOENT)
	perror("remove");

  if(strlen(current_state.persistent_state_modified_filename) > 0)
    if(remove(current_state.persistent_state_modified_filename) < 0)
      if(errno != ENOENT)
	perror("remove");

  if(strlen(current_state.persistent_state_diff_filename) > 0)
    if(remove(current_state.persistent_state_diff_filename) < 0)
      if(errno != ENOENT)
	perror("remove");

  current_state.overlay_filename[0]='\0';
  current_state.persistent_state_filename[0]='\0';
  current_state.persistent_state_modified_filename[0]='\0';
  current_state.persistent_state_diff_filename[0]='\0';

  pthread_mutex_init(&current_state.mutex, NULL);

  return 0;
}


void
catch_sigint(int sig) {
  fprintf(stderr, "(display-launcher) SIGINT caught.  Cleaning up..");
  cleanup();
  exit(EXIT_SUCCESS);
}


DBusGConnection *dbus_conn = NULL;

int
create_dcm_service(char *name, unsigned short port) {
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


  memset(&current_state, 0, sizeof(kimberley_state_t));
  pthread_mutex_init(&current_state.mutex, NULL);


  signal(SIGINT, catch_sigint);

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


  while(1) {

    fprintf(stderr, "(display-launcher) registering with DCM..\n");
    
    if(create_dcm_service(LAUNCHER_DCM_SERVICE_NAME, port) < 0) {
      struct timeval tv;

      fprintf(stderr, "(display-launcher) failed sending message to DCM. "
	      "Sleeping one second and trying again..\n");
      
      tv.tv_sec = 1;
      tv.tv_usec = 0;

      select(0, NULL, NULL, NULL, &tv);

      continue;
    }
    
    fprintf(stderr, "(display-launcher) Accepting DCM connection..\n");
    
  
    dcm_connfd = accept(listenfd, NULL, NULL);
    if(dcm_connfd < 0) {
      perror("accept");
      return -1;
    }
  
    fprintf(stderr, "(display-launcher) Tunneling..\n");
  
    local_tunnel(dcm_connfd, rpc_connfd);
  
    fprintf(stderr, "(display-launcher) A connection was closed.\n");

    if(cleanup() < 0) {
      fprintf(stderr, "(display-launcher) Unable to cleanup from the last "
	      "connection.  Killing the process..\n");
      exit(EXIT_FAILURE);
    }
  }
  
  return -1;
}
