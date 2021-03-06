/*
 *  Kimberley
 *
 *  Copyright (c) 2008-2009 Carnegie Mellon University
 *  All rights reserved.
 *
 *  Kimberley is free software: you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU General Public License
 *  as published by the Free Software Foundation.
 *
 *  Kimberley is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Kimberley. If not, see <http://www.gnu.org/licenses/>.
 */

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

#include "kcm_dbus_app_glue.h"
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

  if(strlen(current_state.overlay_location) > 0)
    if(remove(current_state.overlay_location) < 0)
      if(errno != ENOENT)
	perror("remove");

  if(strlen(current_state.encryption_key_filename) > 0)
    if(remove(current_state.encryption_key_filename) < 0)
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

  current_state.overlay_location[0]='\0';
  current_state.encryption_key_filename[0]='\0';
  current_state.persistent_state_filename[0]='\0';
  current_state.persistent_state_modified_filename[0]='\0';
  current_state.persistent_state_diff_filename[0]='\0';

  pthread_mutex_init(&current_state.mutex, NULL);

  log_deinit();

  return 0;
}


void
catch_sigint(int sig) {
  fprintf(stderr, "\n(display-launcher) SIGINT caught.  Cleaning up..\n");
  cleanup();
  exit(EXIT_SUCCESS);
}


DBusGConnection *dbus_conn = NULL;

int
create_kcm_service(char *name, unsigned short port) {
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
	    "KCM (name=%s, port=%u)..\n", name, port);

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
    
    gport = port;
    
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

int
main(int argc, char *argv[])
{
  int                listenfd, kcm_connfd, rpc_connfd;
  struct sockaddr_in sa;
  int                err;
  unsigned short     port;


  if(log_init() < 0) {
    fprintf(stderr, "(display-launcher) Couldn't initialize log!\n");
    exit(EXIT_FAILURE);
  }

  log_message("display launcher started up..");


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

    fprintf(stderr, "(display-launcher) registering with KCM..\n");
    
    if(create_kcm_service(LAUNCHER_KCM_SERVICE_NAME, port) < 0) {
      struct timeval tv;

      fprintf(stderr, "(display-launcher) failed sending message to KCM. "
	      "Sleeping one second and trying again..\n");
      
      tv.tv_sec = 1;
      tv.tv_usec = 0;

      select(0, NULL, NULL, NULL, &tv);

      continue;
    }
    
    fprintf(stderr, "(display-launcher) Accepting KCM connection..\n");
    
  
    kcm_connfd = accept(listenfd, NULL, NULL);
    if(kcm_connfd < 0) {
      perror("accept");
      return -1;
    }
  
    fprintf(stderr, "(display-launcher) Tunneling..\n");
  
    local_tunnel(kcm_connfd, rpc_connfd);
  
    fprintf(stderr, "(display-launcher) A connection was closed.\n");

    if(cleanup() < 0) {
      fprintf(stderr, "(display-launcher) Unable to cleanup from the last "
	      "connection.  Killing the process..\n");
      exit(EXIT_FAILURE);
    }
  }
  
  return -1;
}
