#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <libgen.h>
#include <math.h>
#include <netdb.h>
#include <time.h>
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
  fprintf(stderr, "%s [-d floppy-file] <[-f patch-file] || [-i URL]> "
	  "<vm-name>\n", argv0);
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


/*
 * Perform a series of pings to determine the round-trip latency of
 * using the current connection through the DCM.  If this is below
 * some threshold, the system will prompt the user to optionally use
 * a USB cable for the thin client connection instead.
 */

float
determine_rtt(CLIENT *clnt) {
  float rtt = 0;
  enum clnt_stat retval;
  struct timeval tv_before, tv_after;
  int ret, i;

  for(i=0; i<10; i++) {  //perform 10 RPC pings

    memset(&tv_before, 0, sizeof(struct timeval));
    memset(&tv_after, 0, sizeof(struct timeval));
  
    ret = gettimeofday(&tv_before, NULL);
    if(ret < 0) {
      perror("gettimeofday");
      return (float) -1;
    }

    fprintf(stderr, "Ping!\n");
    retval = ping_1((void *)NULL, clnt);
    if(retval != RPC_SUCCESS) {
      fprintf(stderr, "(mobile-launcher) ping failed!\n");
      return (float) -1;
    }
    
    ret = gettimeofday(&tv_after, NULL);
    if(ret < 0) {
      perror("gettimeofday");
      return (float) -1;
    }

    rtt += (tv_after.tv_sec - tv_before.tv_sec)*1000;
    rtt += ((double)(tv_after.tv_usec - tv_before.tv_usec))/1000;
  }

  rtt /= 10;  //average over 10 runs
  fprintf(stderr, "(mobile-launcher) Average ping time: %f ms\n", rtt);

  return rtt;
}

#define CHUNK_SIZE 10485760
int
send_file_in_pieces(char *pathname, CLIENT *clnt) {
  struct stat buf;
  int i, n, ret;
  FILE *fp;
  char *bname;
  enum clnt_stat retval;

  if((pathname == NULL) || (clnt == NULL))
    return -1;

  memset(&buf, 0, sizeof(struct stat));
  
  ret = stat(pathname, &buf);
  if(ret < 0) {
    perror("stat");
    return -1;
  }

  fp = fopen(pathname, "r");
  if(fp == NULL) {
    perror("fopen");
    return -1;
  }

  bname = basename(pathname);
  if(bname == NULL) {
    perror("basename");
    return -1;
  }

  n = (int) ceilf(((float)buf.st_size)/((float)CHUNK_SIZE));
  printf(stderr, "(mobile-launcher) Transfer of %s (size=%d) will take %d"
	 " RPCs.\n", pathname, buf.st_size, n);

  retval = send_file_1(bname, buf.st_size, &ret, clnt);
  if(retval != RPC_SUCCESS) {
    clnt_perror (clnt, "send_partial RPC call failed");
    return -1;
  }  

  for(i=0; i<n; i++) {
    char partial_bytes[CHUNK_SIZE];
    data partial_data;
    int num_bytes;

    num_bytes = fread(partial_bytes, 1, CHUNK_SIZE, fp); //1 megabyte
    if(num_bytes < 0) {
      perror("fread");
      return -1;
    }
    if(num_bytes == 0)
      return 0;

    partial_data.data_len = num_bytes;
    partial_data.data_val = partial_bytes;

    retval = send_partial_1(partial_data, &ret, clnt);
    if(retval != RPC_SUCCESS) {
      clnt_perror (clnt, "send_partial RPC call failed");
      return -1;
    }

    fprintf(stderr, ".");
  }

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
  int use_USB = 0;
  char port_str[NI_MAXSERV];
  struct addrinfo *info = NULL, hints;
  enum clnt_stat retval;
  enum vm_type vmt = VM_UNKNOWN;
  char *overlay_path = "", *vm, *floppy_path = "";
  
  int connfd = 0; 
  CLIENT *clnt = NULL;

  if(argc < 4) {
    usage(argv[0]);
    ret = EXIT_FAILURE;
    goto cleanup;
  }
  if((argv[1] == NULL) || (argv[2] == NULL)) {
    usage(argv[0]);
    ret = EXIT_FAILURE;
    goto cleanup;
  }
  

  fprintf(stderr, "(mobile-launcher) starting up..\n");
  
  while((opt = getopt(argc, argv, "f:i:d:")) != -1) {

    switch(opt) {

    case 'f':
      if(vmt != VM_UNKNOWN) {
	usage(argv[0]);
	exit(EXIT_FAILURE);
      }
      vmt = VM_FILE;
      overlay_path = optarg;
      fprintf(stderr, "\toverlay file:%s\n", overlay_path);
      break;
      
    case 'i':
      if(vmt != VM_UNKNOWN) {
	usage(argv[0]);
	exit(EXIT_FAILURE);
      }
      vmt = VM_URL;
      overlay_path = optarg;
      fprintf(stderr, "\toverlay URL:%s\n", overlay_path);
      break;
      
    case 'd':
      floppy_path = optarg;
      fprintf(stderr, "\tfloppy disk path:%s\n", floppy_path);
      break;

    default:
      usage(argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  vm = argv[optind];

  
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
    
  memset(&hints, 0, sizeof(struct addrinfo));
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


  if(determine_rtt(clnt) > 1000) {
    fprintf(stderr, "(mobile-launcher) Connection is slower than 1000ms\n");
    //use_USB = ask_user_for_USB();
  }
  else {
    fprintf(stderr, "(mobile-launcher) Connection is faster than 1000ms\n");
  }


  if(floppy_path != NULL) {
    fprintf(stderr, "(mobile-launcher) Sending floppy disk image..\n");
    if(send_file_in_pieces(floppy_path, clnt) < 0)  //not a total failure case
      fprintf(stderr, "(mobile-launcher) failed sending floppy disk image\n");
  }

  switch(vmt) {

  case VM_FILE:
    fprintf(stderr, "(mobile-launcher) Sending VM overlay..\n");
    if(send_file_in_pieces(overlay_path, clnt) < 0) {
      fprintf(stderr, "(mobile-launcher) failed sending VM overlay!\n");
      ret = EXIT_FAILURE;
      goto cleanup;
    }

    fprintf(stderr, "(mobile-launcher) Loading VM..\n");
    retval = load_vm_from_attachment_1(vm, overlay_path, &err, clnt);
    if (retval != RPC_SUCCESS) {
      fprintf(stderr, "(mobile-launcher) load VM from attachment failed: %s", 
	      clnt_sperrno(retval));
      ret = EXIT_FAILURE;
      goto cleanup;
    }
    break;

  case VM_URL:
    fprintf(stderr, "(mobile-launcher) Loading VM..\n");
    retval = load_vm_from_url_1(vm, overlay_path, &err, clnt);
    if (retval != RPC_SUCCESS) {
      fprintf(stderr, "(mobile-launcher) load VM from URL failed: %s", 
	      clnt_sperrno(retval));
      ret = EXIT_FAILURE;
      goto cleanup;
    }
    break;

  default:
    ret = EXIT_FAILURE;
    goto cleanup;
  }

  fprintf(stderr, "(mobile-launcher) DBus calling into DCM for VNC conn..\n");

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

  snprintf(command, ARG_MAX, "vncviewer localhost::%u", gport_vnc);
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
