#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <glib.h>
#include <unistd.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include "dcm_dbus_app_glue.h"
#include "rpc_mobile_launcher.h"
#include "common.h"

char command[ARG_MAX];

enum vm_type {
  VM_UNKNOWN = 0,
  VM_URL = 1,
  VM_FILE = 2
};

void
usage(char *argv0) {
  fprintf(stderr, "%s <[-f file] || [-i URL]> <vm-name>\n", 
	  argv0);
}


int
perform_authentication(void) {
  int err;
  char command[ARG_MAX];

  snprintf(command, ARG_MAX, "auth.py");
  err = system(command);
  if(err < 0)
    return -1;

  return 0;
}


int
main(int argc, char *argv[])
{
  DBusGConnection *dbus_conn;
  DBusGProxy *dbus_proxy = NULL;
  GError *gerr = NULL;
  guint gport_rpc = 0, gport_vnc = 0;
  int err, ret = EXIT_SUCCESS, opt;
  char port_str[NI_MAXSERV];
  struct addrinfo *info = NULL, hints;
  enum clnt_stat retval;
  enum vm_type vmt = VM_UNKNOWN;
  char *path = "", *vm;
  
  int connfd = 0; 
  CLIENT *clnt = NULL;

  if(argc != 4) {
    usage(argv[0]);
    ret = EXIT_FAILURE;
    goto cleanup;
  }
  if((argv[1] == NULL) || (argv[2] == NULL)) {
    usage(argv[0]);
    ret = EXIT_FAILURE;
    goto cleanup;
  }
  

  while((opt = getopt(argc, argv, "f:i:")) != -1) {

    switch(opt) {

    case 'f':
      if(vmt != VM_UNKNOWN) {
	usage(argv[0]);
	exit(EXIT_FAILURE);
      }
      vmt = VM_FILE;
      path = optarg;
      break;
      
    case 'i':
      if(vmt != VM_UNKNOWN) {
	usage(argv[0]);
	exit(EXIT_FAILURE);
      }
      vmt = VM_URL;
      path = optarg;
      break;
      
    default:
      fprintf(stderr, "Bad command-line option.\n");
      exit(EXIT_FAILURE);
    }
  }

  vm = argv[optind];

  
  fprintf(stderr, "(mobile-launcher) starting up (vm=%s, path=%s)..\n",
	  vm, path);
  
  g_type_init();
  
  fprintf(stderr, "(mobile-launcher) connecting to DBus session bus..\n");

  dbus_conn = dbus_g_bus_get(DBUS_BUS_SESSION, &gerr);
  if(dbus_conn == NULL) {
    g_warning("Unable to connect to dbus: %sn", gerr->message);
    ret = EXIT_FAILURE;
    goto cleanup;
  }
  
  fprintf(stderr, "(mobile-launcher) creating DBus proxy to DCM (%s)..\n",
	  DCM_DBUS_SERVICE_NAME);
  
  /* This won't trigger activation! */
  dbus_proxy = dbus_g_proxy_new_for_name(dbus_conn, 
					 DCM_DBUS_SERVICE_NAME, 
					 DCM_DBUS_SERVICE_PATH, 
					 DCM_DBUS_SERVICE_NAME);
  if(dbus_proxy == NULL) {
    fprintf(stderr, "(mobile-launcher) failed creating DBus proxy!\n");
    ret = EXIT_FAILURE;
    goto cleanup;
  }
	
	
  fprintf(stderr, "(mobile-launcher) DBus calling into dcm for RPC conn..\n");

  /* Signal DCM that you would like it to search for an RPC service.
   * The method call will trigger activation.  In other words,
   * if the DCM is not running before this call is made, it will be
   * afterwards, assuming service files are installed correctly. */
  if(!edu_cmu_cs_diamond_opendiamond_dcm_client(dbus_proxy, 
						LAUNCHER_DCM_SERVICE_NAME, 
						&gport_rpc, &gerr)) {
    /* Method failed, the GError is set, let's warn everyone */
    g_warning("(mobile-launcher) dcm->client() method failed: %s", 
	      gerr->message);
    ret = EXIT_FAILURE;
    goto cleanup;
  }

  fprintf(stderr, "(mobile-launcher) DCM client() returned port: %d\n", 
	  gport_rpc);
  
  
  /* Create new loopback connection to the Sun RPC server on the
   * port that it indicated in the D-Bus message. */
  
  if((connfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    ret = EXIT_FAILURE;
    goto cleanup;
  }
    
  bzero(&hints,  sizeof(struct addrinfo));
  hints.ai_flags = AI_CANONNAME;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(port_str, 6, "%u", gport_rpc);
  
  if((err = getaddrinfo("localhost", port_str, &hints, &info)) < 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
    ret = EXIT_FAILURE;
    goto cleanup;
  }
  
  fprintf(stderr, "(mobile-launcher) connect()ing locally to dcm..\n");
	
  if(connect(connfd, info->ai_addr, sizeof(struct sockaddr_in)) < 0) {
    perror("connect");
    ret = EXIT_FAILURE;
    goto cleanup;
  }
  
  fprintf(stderr, "(mobile-launcher) successfully connected. bringing up "
	  "launcher..\n");
  
  clnt = rpc_init(connfd, MOBILELAUNCHER_PROG, MOBILELAUNCHER_VERS);
  if(clnt == NULL) {
    fprintf(stderr, "(mobile-launcher) Sun RPC initialization failed");
    close(connfd);
    ret = EXIT_FAILURE;
    goto cleanup;
  }

  perform_authentication();

  switch(vmt) {

  case VM_FILE:
    retval = load_vm_from_path_1(vm, path, &err, clnt);
    if (retval != RPC_SUCCESS) {
      fprintf(stderr, "mobile_start: call sending failed: %s", 
	      clnt_sperrno(retval));
      ret = EXIT_FAILURE;
      goto cleanup;
    }
    break;

  case VM_URL:
    retval = load_vm_from_url_1(vm, path, &err, clnt);
    if (retval != RPC_SUCCESS) {
      fprintf(stderr, "mobile_start: call sending failed: %s", 
	      clnt_sperrno(retval));
      ret = EXIT_FAILURE;
      goto cleanup;
    }
    break;

  default:
    ret = EXIT_FAILURE;
    goto cleanup;
  }

  fprintf(stderr, "(mobile-launcher) DBus calling into dcm for VNC conn..\n");

  /* Signal DCM that you would like it to search for a VNC service. */
  if(!edu_cmu_cs_diamond_opendiamond_dcm_client(dbus_proxy, 
						VNC_DCM_SERVICE_NAME, 
						&gport_vnc, &gerr)) {
    /* Method failed, the GError is set, let's warn everyone */
    g_warning("(mobile-launcher) dcm->client() method failed: %s", 
	      gerr->message);
    ret = EXIT_FAILURE;
    goto cleanup;
  }

  fprintf(stderr, "(mobile-launcher) DCM client() returned port: %d\n", 
	  gport_vnc);

  fprintf(stderr, "(mobile-launcher) executing vncviwer..\n");

  snprintf(command, ARG_MAX, "vncviewer --hostname localhost:%u", gport_vnc);
  err = system(command);
  if(err < 0) {
    perror("system");
    ret = EXIT_FAILURE;
  }

  
 cleanup:
  if(clnt != NULL)
    end_usage_1(0, (void *)NULL, clnt); // signal display
  

  if(gerr) g_error_free (gerr);
  if(dbus_proxy) g_object_unref(dbus_proxy);
  if(info) freeaddrinfo(info);
  
  exit(ret);
}
