#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <math.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-bindings.h>
#include <glib.h>

#include "dcm_dbus_app_glue.h"
#include "rpc_mobile_launcher.h"
#include "common.h"


#define AVAHI_TIMEOUT 15

char command[ARG_MAX];

enum vm_type {
  VM_UNKNOWN = 0,
  VM_URL = 1,
  VM_FILE = 2
};

void
usage(char *argv0) {
  printf("mobile_launcher [-a floppy-file] [-d encryption-key-file]\n"
	 "                <[-f patch-file] || [-i URL]> <vm-name>\n");
}


int
perform_authentication(void) {
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

  if(clnt == NULL)
    return (float) -1;

  retval = ping_1((void *)NULL, clnt);
  if(retval != RPC_SUCCESS) {
    fprintf(stderr, "(mobile-launcher) ping failed!\n");
    return (float) -1;
  }

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


int
send_file_in_pieces(char *path, CLIENT *clnt) {
  struct stat buf;
  int i, n, ret;
  FILE *fp;
  enum clnt_stat retval;
  char logmsg[ARG_MAX];

  if((path == NULL) || (clnt == NULL))
    return -1;

  memset(&buf, 0, sizeof(struct stat));
  
  ret = stat(path, &buf);
  if(ret < 0) {
    perror("stat");
    return -1;
  }

  fp = fopen(path, "r");
  if(fp == NULL) {
    perror("fopen");
    return -1;
  }

  n = (int) ceilf(((float)buf.st_size)/((float)CHUNK_SIZE));
  fprintf(stderr, "(mobile-launcher) Transfer of %s (size=%d) will take %d"
	  " RPCs.\n", path, (int) buf.st_size, n);

  snprintf(logmsg, ARG_MAX, "mobile launcher requesting send of file, size: %u", buf.st_size);
  log_message(logmsg);

  retval = send_file_1(path, buf.st_size, &ret, clnt);
  if(retval != RPC_SUCCESS) {
    clnt_perror (clnt, "send_file RPC call failed");
    return -1;
  }  

  log_message("mobile launcher completed send request");

  log_message("mobile launcher sending file");

  for(i=0; i<n; i++) {
    char partial_bytes[CHUNK_SIZE];
    data partial_data;
    int num_bytes;

    num_bytes = fread(partial_bytes, 1, CHUNK_SIZE, fp);
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

  log_message("mobile launcher completed send of file");

  return 0;
}


int
retrieve_file_in_pieces(char *path, CLIENT *clnt) {
  int i, n, size=0;
  FILE *fp;
  enum clnt_stat retval;
  char logmsg[ARG_MAX];

  if((path == NULL) || (clnt == NULL))
    return -1;

  fprintf(stderr, "(mobile-launcher) Retrieving file to path: %s\n", path);

  fp = fopen(path, "w");
  if(fp == NULL) {
    perror("fopen");
    return -1;
  }

  log_message("mobile launcher requesting retrieval of file");
  retval = retrieve_file_1(path, &size, clnt);
  if(retval != RPC_SUCCESS) {
    clnt_perror (clnt, "retrieve_file RPC call failed");
    fclose(fp);
    return -1;
  }
  snprintf(logmsg, ARG_MAX, "mobile launcher completed request for retrieval of file, size: %d", size);
  log_message(logmsg);

  n = (int) ceilf(((float)size)/((float)CHUNK_SIZE));
  fprintf(stderr, "(mobile-launcher) Transfer of %s (size=%d) will take %d"
	  " RPCs.\n", path, (int) size, n);

  log_message("mobile launcher retrieving file");
  for(i=0; i<n; i++) {
    data partial_data;
    unsigned int num_bytes;

    memset(&partial_data, 0, sizeof(data));

    retval = retrieve_partial_1(&partial_data, clnt);
    if(retval != RPC_SUCCESS) {
      clnt_perror (clnt, "retrieve_partial RPC call failed");
      fclose(fp);
      return -1;
    }

    num_bytes = fwrite(partial_data.data_val, 1, partial_data.data_len, fp);
    if(num_bytes < partial_data.data_len) {
      perror("fread");
      fclose(fp);
      return -1;
    }

    xdr_free((xdrproc_t)xdr_data, (char *)&partial_data);

    fprintf(stderr, ".");
  }
  log_message("mobile launcher completed retrieving file");

  fclose(fp);

  return 0;
}


int
establish_thin_client_connection(DBusGProxy *dbus_proxy) {
  int i, ret;
  guint g_vnc_port = 0;
  GError *gerr = NULL;


  fprintf(stderr, "(mobile-launcher) Calling into DCM to make "
	  "thin client connection..\n");

  ret = -1;

  for(i=0; i<AVAHI_TIMEOUT; i++) {
    if(!edu_cmu_cs_diamond_opendiamond_dcm_client(dbus_proxy, 
						  VNC_DCM_SERVICE_NAME, 
						  &g_vnc_port, &gerr)) {
      struct timeval tv;

      tv.tv_sec = 1;
      tv.tv_usec = 0;

      g_warning("(mobile-launcher) dcm->client() method failed: %s", 
		gerr->message);
      
      select(0, NULL, NULL, NULL, &tv);
    }
    else {
      ret = g_vnc_port;
      break;
    }
  }

  return ret;
}
	  

int
main(int argc, char *argv[])
{
  DBusGConnection *dbus_conn;
  DBusGProxy *dbus_proxy = NULL;
  GError *gerr = NULL;
  guint g_rpc_port = 0;
  int err, ret = EXIT_SUCCESS, opt;
  int vnc_port;
  int use_USB = 0;
  char port_str[NI_MAXSERV];
  struct addrinfo *info = NULL, hints;
  enum clnt_stat retval;
  enum vm_type vmt = VM_UNKNOWN;

  char *vm;
  char *overlay_path = NULL;
  char *floppy_path = NULL;
  char *encryption_key_path = NULL;

  char logmsg[ARG_MAX];
  
  int connfd = 0;
  int ms;

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
  

  if(log_init() < 0) {
    fprintf(stderr, "(mobile-launcher) Couldn't initialize log!\n");
    exit(EXIT_FAILURE);
  }

  log_message("mobile launcher started up..");


  fprintf(stderr, "(mobile-launcher) starting up..\n");
  
  log_message("mobile launcher parsing options..");

  while((opt = getopt(argc, argv, "a:d:f:i:")) != -1) {

    switch(opt) {

    case 'a':
      floppy_path = optarg;
      fprintf(stderr, "\tfloppy disk file:%s\n", floppy_path);

      {
	char command[ARG_MAX];

	fprintf(stderr, "(mobile-launcher) unmounting floppy..\n");

	snprintf(command, ARG_MAX, "umount %s", floppy_path);
	system(command);
      }

      break;

    case 'd':
      encryption_key_path = optarg;
      fprintf(stderr, "\tencryption key file:%s\n", encryption_key_path);
      break;

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
      
    default:
      usage(argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  vm = argv[optind];
  
  log_message("mobile launcher completed parsing options..");

  log_message("mobile launcher connecting to DBUS..");

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
  

  /*
   * The following D-Bus call will not trigger activation.
   */

  dbus_proxy = dbus_g_proxy_new_for_name(dbus_conn, 
					 DCM_DBUS_SERVICE_NAME, 
					 DCM_DBUS_SERVICE_PATH, 
					 DCM_DBUS_SERVICE_NAME);
  if(dbus_proxy == NULL) {
    fprintf(stderr, "(mobile-launcher) failed creating DBus proxy!\n");
    ret = EXIT_FAILURE;
    goto cleanup;
  }
	
  log_message("mobile launcher completed connecting to DBUS..");
	
  /*
   * Signal DCM that you would like it to search for an RPC service.
   * The method call will trigger activation.  In other words,
   * if the DCM is not running before this call is made, it will be
   * afterwards, assuming service files are installed correctly. 
   */

  fprintf(stderr, "(mobile-launcher) DBus calling into dcm for RPC conn..\n");

  log_message("mobile launcher requesting service discovery from DCM..");

  if(!edu_cmu_cs_diamond_opendiamond_dcm_client(dbus_proxy, 
						LAUNCHER_DCM_SERVICE_NAME, 
						&g_rpc_port, &gerr)) {
    /* Method failed, the GError is set, let's warn everyone */
    g_warning("(mobile-launcher) dcm->client() method failed: %s", 
	      gerr->message);
    ret = EXIT_FAILURE;
    goto cleanup;
  }

  log_message("mobile launcher completed request for service discovery");

  fprintf(stderr, "(mobile-launcher) DCM client() returned port: %d\n", 
	  g_rpc_port);
  
  
  /* Create new loopback connection to the Sun RPC server on the
   * port that it indicated in the D-Bus message. */
  
  log_message("mobile launcher connecting to DCM");

  if((connfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    ret = EXIT_FAILURE;
    goto cleanup;
  }
    
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_flags = AI_CANONNAME;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(port_str, 6, "%u", g_rpc_port);
  
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

  log_message("mobile launcher completed connecting to DCM");
  
  fprintf(stderr, "(mobile-launcher) successfully connected. bringing up "
	  "launcher..\n");
  

  log_message("mobile launcher establishing connection to display");

  clnt = convert_socket_to_rpc_client(connfd, MOBILELAUNCHER_PROG, 
				      MOBILELAUNCHER_VERS);
  if(clnt == NULL) {
    fprintf(stderr, "(mobile-launcher) Sun RPC initialization failed");
    close(connfd);
    ret = EXIT_FAILURE;
    goto cleanup;
  }

  //perform_authentication();

  retval = ping_1((void *)NULL, clnt);
  if(retval != RPC_SUCCESS) {
    fprintf(stderr, "(mobile-launcher) ping failed!\n");
    return (float) -1;
  }

  log_message("mobile launcher completed establishing connection to display");


  log_message("mobile launcher calculating latency");

  ms = determine_rtt(clnt);
  if(ms > 1000) {
    fprintf(stderr, "(mobile-launcher) Connection is slower than 1000ms\n");
    //use_USB = ask_user_for_USB();
  }
  else {
    fprintf(stderr, "(mobile-launcher) Connection is faster than 1000ms\n");
  }

  snprintf(logmsg, ARG_MAX, "mobile launcher completed calculating latency: %u ms", ms);
  log_message(logmsg);

  /*
   * Send a floppy disk filesystem image to be attached to a running
   * virtual machine.
   */

  if(floppy_path != NULL) {
    fprintf(stderr, "(mobile-launcher) Sending floppy disk image..\n");
    
    log_message("mobile launcher sending floppy disk");
    if(send_file_in_pieces(floppy_path, clnt) < 0) {
      fprintf(stderr, "(mobile-launcher) failed sending floppy disk image file\n");
      floppy_path = NULL;
    }
    else {
      log_message("mobile launcher completed sending floppy disk");
      log_message("mobile launcher indicating floppy disk filename");
      retval = use_persistent_state_1(floppy_path, &err, clnt);
      if (retval != RPC_SUCCESS) {
	fprintf(stderr, "(mobile-launcher) setting persistent state file "
		"failed: %s\n", clnt_sperrno(retval));
	floppy_path = NULL;
      }
      log_message("mobile launcher completed indicating floppy disk filename");
    }
  }


  /*
   * Send an encryption key capable of decoding the virtual machine overlay.
   */

  if(encryption_key_path != NULL) {
    fprintf(stderr, "(mobile-launcher) Sending encryption key..\n");
    
    log_message("mobile launcher sending encryption key");
    if(send_file_in_pieces(encryption_key_path, clnt) < 0) {
      fprintf(stderr, "(mobile-launcher) failed sending encryption key file\n");
      floppy_path = NULL;
    }
    else {
      log_message("mobile launcher completed sending encryption key");
      log_message("mobile launcher indicating encryption key filename");
      retval = use_encryption_key_1(encryption_key_path, &err, clnt);
      if (retval != RPC_SUCCESS) {
	fprintf(stderr, "(mobile-launcher) setting encryption key file "
		"failed: %s\n", clnt_sperrno(retval));
	encryption_key_path = NULL;
	goto cleanup;
      }
      log_message("mobile launcher completed indicating encryption key filename");
    }
  }


  /*
   * Transfer the VM overlay to the display.
   */

  switch(vmt) {

  case VM_FILE:
    fprintf(stderr, "(mobile-launcher) Sending VM overlay..\n");
    log_message("mobile launcher sending VM overlay");
    if(send_file_in_pieces(overlay_path, clnt) < 0) {
      fprintf(stderr, "(mobile-launcher) failed sending VM overlay!\n");
      ret = EXIT_FAILURE;
      goto cleanup;
    }
    log_message("mobile launcher completed sending VM overlay");

    fprintf(stderr, "(mobile-launcher) Loading VM..\n");
    log_message("mobile launcher loading VM");
    retval = load_vm_from_attachment_1(vm, overlay_path, &err, clnt);
    if (retval != RPC_SUCCESS) {
      fprintf(stderr, "(mobile-launcher) load VM from attachment failed: %s", 
	      clnt_sperrno(retval));
      ret = EXIT_FAILURE;
      goto cleanup;
    }
    log_message("mobile launcher completed loading VM");
    break;

  case VM_URL:
    fprintf(stderr, "(mobile-launcher) Loading VM..\n");
    log_message("mobile launcher loading VM");
    retval = load_vm_from_url_1(vm, overlay_path, &err, clnt);
    if (retval != RPC_SUCCESS) {
      fprintf(stderr, "(mobile-launcher) load VM from URL failed: %s", 
	      clnt_sperrno(retval));
      ret = EXIT_FAILURE;
      goto cleanup;
    }
    log_message("mobile launcher completed loading VM");
    break;

  default:
    ret = EXIT_FAILURE;
    goto cleanup;
  }


  /*
   * Signal DCM that you would like it to search for a VNC service.
   */

  log_message("mobile launcher requesting into DCM for thin client connection");
  vnc_port = establish_thin_client_connection(dbus_proxy);
  if(vnc_port < 0) {
    fprintf(stderr, "(mobile-launcher) DCM couldn't discover thin client "
	    "services!\n");
    ret = EXIT_FAILURE;
    goto cleanup;
  }
  else {
    fprintf(stderr, "(mobile-launcher) DCM client() returned port: %d\n", 
	    vnc_port);
  }
  log_message("mobile launcher completed request into DCM for thin client connection");

  snprintf(command, ARG_MAX, "vncviewer localhost::%u", vnc_port);
  fprintf(stderr, "(mobile-launcher) executing: %s\n", command);
  log_message("mobile launcher executing VNCviewer");
  err = system(command);
  if(err < 0) {
    perror("system");
    ret = EXIT_FAILURE;
  }
  log_message("mobile launcher completed executing VNCviewer");

  
 cleanup:
  if(clnt != NULL) {
    char  *diff_filename = NULL;


    /*
     * Terminate remote dekimberlize process, retrieving modified
     * persistent state, if necessary.
     */

    if(floppy_path != NULL) {
      char  diff_filename_local[PATH_MAX];
      
      log_message("mobile launcher indicating end of use to display");
      end_usage_1(1, &diff_filename, clnt);
      log_message("mobile launcher completed indicating end of use to display");
      if(diff_filename != NULL) {
	char *bname;
	char command[ARG_MAX];
	char floppy_sum_path[PATH_MAX];

	bname = basename(diff_filename);
	fprintf(stderr, "(mobile-launcher) server indicated persistent diff was %s\n", diff_filename);
	
	diff_filename_local[0] = '\0';
	snprintf(diff_filename_local, PATH_MAX, "/tmp/%s", bname);

	log_message("mobile launcher retrieving persistent state delta");
	if(retrieve_file_in_pieces(diff_filename_local, clnt) < 0) {
	  fprintf(stderr, "(mobile-launcher) Couldn't retrieve '%s'\n",
		  diff_filename);
	}
	log_message("mobile launcher completed retrieving persistent state delta");

	fprintf(stderr, "(mobile-launcher) applying pers. state patch..\n");
	snprintf(floppy_sum_path, PATH_MAX, "%s.new", floppy_path);
	snprintf(command, ARG_MAX, "xdelta patch %s %s %s", 
		 diff_filename_local, floppy_path, floppy_sum_path);
	log_message("mobile launcher applying persistent state delta");
	system(command);
	log_message("mobile launcher completed applying persistent state delta");

	remove(floppy_path);
	rename(floppy_sum_path, floppy_path);

	fprintf(stderr, "(mobile-launcher) remounting state image..\n");
	snprintf(command, ARG_MAX, "mount %s", floppy_path);
	log_message("mobile launcher remounting persistent state");
	system(command);
	log_message("mobile launcher remounting persistent state");
      }
      else {
	fprintf(stderr, "(mobile-launcher) no persistent state path returned to retrieve!\n");
      }
    }
    else {
      log_message("mobile launcher indicating end of use to display");
      end_usage_1(0, &diff_filename, clnt);
      log_message("mobile launcher completed indicating end of use to display");
    }

    xdr_free((xdrproc_t) xdr_wrapstring, (char *)&diff_filename);

    clnt_destroy(clnt);
    clnt = NULL;
  }
  

  if(gerr) g_error_free (gerr);
  if(dbus_proxy) g_object_unref(dbus_proxy);
  if(info) freeaddrinfo(info);
  
  exit(ret);
}
