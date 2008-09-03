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

#include <minirpc/minirpc.h>
#include "rpc_mobile_launcher_client.h"

#include "kcm_dbus_app_glue.h"
#include "common.h"


#define AVAHI_TIMEOUT 15

char command[ARG_MAX];

enum vm_type {
    VM_UNKNOWN = 0,
    VM_URL = 1,
    VM_FILE = 2
};

static void usage(void)
{
    printf("mobile_launcher [-a floppy-file] [-d encryption-key-file]\n"
	   "\t\t<[-f patch-file] || [-i URL]> <vm-name>\n");
}


/*
 * Perform a series of pings to determine the round-trip latency of
 * using the current connection through the KCM.  If this is below
 * some threshold, the system will prompt the user to optionally use
 * a USB cable for the thin client connection instead.
 */

static int determine_rtt(struct mrpc_connection *conn)
{
    int rtt = 0;
    mrpc_status_t retval;
    struct timeval tv_before, tv_after;
    int ret, i;

    ret = gettimeofday(&tv_before, NULL);
    if (ret < 0) {
	perror("gettimeofday");
	return -1.0;
    }

#define NPINGS 10 //perform 10 RPC pings
    for (i = 0; i < NPINGS; i++) {
	fprintf(stderr, "Ping!\n");

	retval = rpc_mobile_launcher_ping(conn);
	if (retval != MINIRPC_OK) {
	    fprintf(stderr, "(mobile-launcher) ping failed!\n");
	    return -1.0;
	}
    }

    ret = gettimeofday(&tv_after, NULL);
    if(ret < 0) {
	perror("gettimeofday");
	return -1.0;
    }

    if (tv_after.tv_usec < tv_before.tv_usec) {
	tv_after.tv_sec--;
	tv_after.tv_usec += 1000000;
    }
    tv_after.tv_sec  -= tv_before.tv_sec;
    tv_after.tv_usec -= tv_before.tv_usec;

    rtt = (tv_after.tv_sec * 1000) + (tv_after.tv_usec / 1000);
    rtt /= NPINGS;

    fprintf(stderr, "(mobile-launcher) Average ping time: %u ms\n", rtt);
    return rtt;
}


static int send_file_in_pieces(struct mrpc_connection *conn, char *path)
{
    mrpc_status_t retval;
    struct stat statbuf;
    send_x chunk;
    char buf[CHUNK_SIZE];
    ssize_t n;
    int fd;

    chunk.name = path;
    chunk.offset = 0;
    chunk.data.data_val = buf;
    chunk.data.data_len = 0;

    fd = open(path, O_RDONLY);
    if (fd == -1) {
	perror("open");
	return -1;
    }

    if (fstat(fd, &statbuf) == -1) {
	perror("fstat");
	close(fd);
	return -1;
    }

    n = (statbuf.st_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    fprintf(stderr, "(mobile-launcher) Transfer of %s (size=%d) will take %d"
	    " RPCs.\n", path, (int)statbuf.st_size, n);

    log_message("mobile launcher sending file");

    while (1) {
	n = read(fd, buf, CHUNK_SIZE);
	if (n <= 0) break;
	chunk.data.data_len = n;

	retval = rpc_mobile_launcher_send_chunk(conn, &chunk);
	if (retval) {
	    fprintf(stderr, "\n(mobile-launcher) transfer failed\n");
	    close(fd);
	    return -1;
	}

	fprintf(stderr, ".");
	chunk.offset += n;
    }
    close(fd);
    fprintf(stderr, "\n(mobile-launcher) transfer complete\n");

    log_message("mobile launcher completed send of file");
    return 0;
}


static int retrieve_file_in_pieces(struct mrpc_connection *conn, char *path)
{
    mrpc_status_t retval;
    retrieve_x retrieve;
    data *chunk;
    ssize_t n;
    int fd;

    fprintf(stderr, "(mobile-launcher) Retrieving file to path: %s\n", path);

    fd = open(path, O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW, S_IRUSR|S_IWUSR);
    if (fd == -1) {
	perror("open");
	return -1;
    }

    retrieve.name = path;
    retrieve.offset = 0;

    fprintf(stderr, "\n(mobile-launcher) retrieving file %s\n", path);

    log_message("mobile launcher retrieving file");

    while (1)
    {
	retval = rpc_mobile_launcher_retrieve_chunk(conn, &retrieve, &chunk);
	if(retval != MINIRPC_OK) {
	    close(fd);
	    return -1;
	}

	if (chunk->data_len == 0)
	    break;

	n = write(fd, chunk->data_val, chunk->data_len);
	if(n != (ssize_t)chunk->data_len) {
	    perror("read");
	    free_data(chunk, 1);
	    close(fd);
	    return -1;
	}

	retrieve.offset += chunk->data_len;

	free_data(chunk, 1);
	fprintf(stderr, ".");
    }
    close(fd);
    fprintf(stderr, "\n(mobile-launcher) transfer complete\n");

    log_message("mobile launcher completed retrieving file");
    return 0;
}


static int
establish_thin_client_connection(DBusGProxy *dbus_proxy, int iface)
{
  int i, ret;
  guint gport = 0;
  gint interface = iface;
  GError *gerr = NULL;


  fprintf(stderr, "(mobile-launcher) Calling into KCM to make "
	  "thin client connection..\n");

  ret = -1;

  for(i=0; i<AVAHI_TIMEOUT; i++) {

    if(!edu_cmu_cs_kimberley_kcm_browse(dbus_proxy,
					VNC_DCM_SERVICE_NAME,
					interface,
					&gport,
					&gerr)) {
      struct timeval tv;

      tv.tv_sec = 1;
      tv.tv_usec = 0;

      g_warning("(mobile-launcher) kcm->browse() method failed: %s",
		gerr->message);

      select(0, NULL, NULL, NULL, &tv);
    }
    else {
      ret = gport;
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
    guint gport = 0;
    gchar **interface_strs = NULL;
    gint interface = -1;
    int err, ret = EXIT_SUCCESS, opt, i;
    int vnc_port;
    int usb_idx = -1;
    enum clnt_stat retval;
    enum vm_type vmt = VM_UNKNOWN;

    char *vm;
    char *overlay_path = NULL;
    char *floppy_path = NULL;
    char *floppy_compressed_path;
    char *encryption_key_path = NULL;

    char logmsg[ARG_MAX];

    int ms;

    struct mrpc_conn_set *mrpc_cset;
    struct mrpc_connection *conn = NULL;
    char svc[NI_MAXSERV];


    if(argc < 4) {
	usage();
	ret = EXIT_FAILURE;
	goto cleanup;
    }
    if((argv[1] == NULL) || (argv[2] == NULL)) {
	usage();
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

		log_message("mobile launcher unmounting floppy..");

		snprintf(command, ARG_MAX, "umount %s", floppy_path);
		system(command);

		log_message("mobile launcher compressing floppy..");
		floppy_compressed_path = compress_file(floppy_path);
		log_message("mobile launcher completed compressing floppy..");
	    }

	    break;

	case 'd':
	    encryption_key_path = optarg;
	    fprintf(stderr, "\tencryption key file:%s\n", encryption_key_path);
	    break;

	case 'f':
	    if(vmt != VM_UNKNOWN) {
		usage();
		exit(EXIT_FAILURE);
	    }
	    vmt = VM_FILE;
	    overlay_path = optarg;
	    fprintf(stderr, "\toverlay file:%s\n", overlay_path);
	    break;

	case 'i':
	    if(vmt != VM_UNKNOWN) {
		usage();
		exit(EXIT_FAILURE);
	    }
	    vmt = VM_URL;
	    overlay_path = optarg;
	    fprintf(stderr, "\toverlay URL:%s\n", overlay_path);
	    break;

	default:
	    usage();
	    exit(EXIT_FAILURE);
	}
    }

    vm = argv[optind];

    log_message("mobile launcher completed parsing options..");

    log_message("mobile launcher connecting to DBUS..");

    fprintf(stderr, "(mobile-launcher) connecting to DBus session bus..\n");

    g_type_init();

    dbus_conn = dbus_g_bus_get(DBUS_BUS_SESSION, &gerr);
    if(dbus_conn == NULL) {
	g_warning("Unable to connect to dbus: %sn", gerr->message);
	ret = EXIT_FAILURE;
	goto cleanup;
    }

    fprintf(stderr, "(mobile-launcher) creating DBus proxy to KCM (%s)..\n",
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
     * Signal KCM that you would like it to search for an RPC service.
     * The method call will trigger activation.  In other words,
     * if the KCM is not running before this call is made, it will be
     * afterwards, assuming service files are installed correctly.
     */
    log_message("mobile launcher establishing connection to display through KCM");

    fprintf(stderr, "(mobile-launcher) DBus calling into kcm (sense)..\n");

    /* The method call will trigger activation, more on that later */
    if (!edu_cmu_cs_kimberley_kcm_sense(dbus_proxy, &interface_strs, &gerr))
    {
	/* Method failed, the GError is set, let's warn everyone */
	g_warning("(mobile-launcher) kcm->sense() method failed: %s",
		  gerr->message);
	ret = EXIT_FAILURE;
	goto cleanup;
    }

    if (interface_strs != NULL) {
	fprintf(stderr, "(mobile-launcher) Found some interfaces:\n");
	for (i=0; interface_strs[i] != NULL; i++) {
	    fprintf(stderr, "\t%d: %s\n", i, interface_strs[i]);
	    if (!strcmp(interface_strs[i], "usb0"))
		usb_idx = i;
	}
	fprintf(stderr, "\n");
    }

    fprintf(stderr, "(mobile-launcher) DBus calling into kcm (browse)..\n");

    if (!edu_cmu_cs_kimberley_kcm_browse(dbus_proxy, LAUNCHER_DCM_SERVICE_NAME,
					 interface, &gport, &gerr))
    {
	/* Method failed, the GError is set, let's warn everyone */
	g_warning("(mobile-launcher) kcm->client() method failed: %s",
		  gerr->message);
	ret = EXIT_FAILURE;
	goto cleanup;
    }

    fprintf(stderr, "(mobile-launcher) KCM client() returned port: %u\n",
	    gport);

    if (mrpc_conn_set_create(&mrpc_cset, rpc_mobile_launcher_client, NULL)) {
	fprintf(stderr, "Failed to intialize minirpc connection set\n");
	exit(EXIT_FAILURE);
    }

    if (mrpc_start_dispatch_thread(mrpc_cset)) {
	fprintf(stderr, "Failed to start minirpc dispatch thread\n");
	exit(EXIT_FAILURE);
    }

    if (mrpc_conn_create(&conn, mrpc_cset, NULL)) {
	fprintf(stderr, "Failed to start minirpc dispatch thread\n");
	exit(EXIT_FAILURE);
    }

    snprintf(svc, sizeof(svc), "%u", gport);
    if (mrpc_connect(conn, AF_INET, NULL, svc)) {
	fprintf(stderr, "Failed to connect to display\n");
	exit(EXIT_FAILURE);
    }

    //perform_authentication();

    retval = rpc_mobile_launcher_ping(conn);
    if(retval != MINIRPC_OK) {
	fprintf(stderr, "(mobile-launcher) ping failed!\n");
	exit(EXIT_FAILURE);
    }

    log_message("mobile launcher completed establishing connection to display");


    log_message("mobile launcher calculating latency");

    /*
     * If the round trip time of a null RPC is greater than 100 milliseconds,
     * try using USB networking for the thin client connection; better latency
     * is possible.
     */

    ms = determine_rtt(conn);
    if (ms > 100) {
	fprintf(stderr, "(mobile-launcher) Connection is slower than 100ms\n");
	interface = usb_idx;
    } else {
	fprintf(stderr, "(mobile-launcher) Connection is faster than 100ms\n");
    }

    snprintf(logmsg, ARG_MAX, "mobile launcher completed calculating latency: %u ms", ms);
    log_message(logmsg);

    /*
     * Send a floppy disk filesystem image to be attached to a running
     * virtual machine.
     */

    if (floppy_path != NULL) {
	fprintf(stderr, "(mobile-launcher) Sending floppy disk image..\n");

	log_message("mobile launcher sending compressed floppy disk");
	if (send_file_in_pieces(conn, floppy_compressed_path) < 0) {
	    fprintf(stderr, "(mobile-launcher) failed sending compressed "
		    "floppy disk image file\n");
	    floppy_path = NULL;
	}
	else {
	    log_message("mobile launcher completed sending floppy disk");
	    log_message("mobile launcher indicating floppy disk filename");
	    retval = rpc_mobile_launcher_use_persistent_state(conn,
						    &floppy_compressed_path);
	    if (retval) {
		fprintf(stderr, "(mobile-launcher) setting persistent state "
			"file failed\n");
		floppy_path = NULL;
	    }
	    log_message("mobile launcher completed indicating floppy disk "
			"filename");
	}
    }


    /*
     * Send an encryption key capable of decoding the virtual machine overlay.
     */

    if(encryption_key_path != NULL) {
	fprintf(stderr, "(mobile-launcher) Sending encryption key..\n");

	log_message("mobile launcher sending encryption key");
	if(send_file_in_pieces(conn, encryption_key_path) < 0) {
	    fprintf(stderr, "(mobile-launcher) failed sending encryption key file\n");
	    floppy_path = NULL;
	}
	else {
	    log_message("mobile launcher completed sending encryption key");
	    log_message("mobile launcher indicating encryption key filename");
	    retval = rpc_mobile_launcher_use_encryption_key(conn,
							&encryption_key_path);
	    if (retval) {
		fprintf(stderr, "(mobile-launcher) setting encryption key file "
			"failed\n");
		encryption_key_path = NULL;
		goto cleanup;
	    }
	    log_message("mobile launcher completed indicating encryption key filename");
	}
    }


    /*
     * Transfer the VM overlay to the display.
     */
    struct load_vm_in lvi;

    switch(vmt) {

    case VM_FILE:
	fprintf(stderr, "(mobile-launcher) Sending VM overlay..\n");
	log_message("mobile launcher sending VM overlay");
	if(send_file_in_pieces(conn, overlay_path) < 0) {
	    fprintf(stderr, "(mobile-launcher) failed sending VM overlay!\n");
	    ret = EXIT_FAILURE;
	    goto cleanup;
	}
	log_message("mobile launcher completed sending VM overlay");

	fprintf(stderr, "(mobile-launcher) Loading VM..\n");
	log_message("mobile launcher loading VM");

	lvi.vm_name = vm;
	lvi.uri = overlay_path;
	retval = rpc_mobile_launcher_load_vm_from_attachment(conn,&lvi);
	if (retval != RPC_SUCCESS) {
	    fprintf(stderr, "(mobile-launcher) load VM from attachment failed\n");
	    ret = EXIT_FAILURE;
	    goto cleanup;
	}
	log_message("mobile launcher completed loading VM");
	break;

    case VM_URL:
	fprintf(stderr, "(mobile-launcher) Loading VM..\n");
	log_message("mobile launcher loading VM");

	lvi.vm_name = vm;
	lvi.uri = overlay_path;
	retval = rpc_mobile_launcher_load_vm_from_URL(conn, &lvi);
	if (retval != RPC_SUCCESS) {
	    fprintf(stderr, "(mobile-launcher) load VM from URL failed\n");
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
     * Signal KCM that you would like it to search for a VNC service.
     */

    log_message("mobile launcher requesting into KCM for thin client connection");
    vnc_port = establish_thin_client_connection(dbus_proxy, interface);
    if(vnc_port < 0) {
	fprintf(stderr, "(mobile-launcher) KCM couldn't discover thin client "
		"services!\n");
	ret = EXIT_FAILURE;
	goto cleanup;
    }
    else {
	fprintf(stderr, "(mobile-launcher) KCM client() returned port: %d\n",
		vnc_port);
    }
    log_message("mobile launcher completed request into KCM for thin client connection");

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
    if (conn) {
	retrieve_state retrieve_state;
	state *diff_filename = NULL;

	/*
	 * Terminate remote dekimberlize process, retrieving modified
	 * persistent state, if necessary.
	 */

	if(floppy_path != NULL) {
	    char  diff_filename_local[PATH_MAX];

	    log_message("mobile launcher indicating end of use to display");
	    retrieve_state = TRUE;
	    rpc_mobile_launcher_end_usage(conn, &retrieve_state,&diff_filename);
	    log_message("mobile launcher completed indicating end of use to display");
	    if (diff_filename && *diff_filename) {
		char *bname;
		char command[ARG_MAX];
		char floppy_sum_path[PATH_MAX];

		bname = basename(*diff_filename);
		fprintf(stderr, "(mobile-launcher) server indicated persistent "
			"diff was %s\n", *diff_filename);

		diff_filename_local[0] = '\0';
		snprintf(diff_filename_local, PATH_MAX, "/tmp/%s", bname);

		log_message("mobile launcher retrieving persistent state delta");
		if (retrieve_file_in_pieces(conn, diff_filename_local) < 0) {
		    fprintf(stderr, "(mobile-launcher) Couldn't retrieve '%s'\n",
			    *diff_filename);
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
	    retrieve_state = FALSE;
	    rpc_mobile_launcher_end_usage(conn,&retrieve_state, &diff_filename);
	    log_message("mobile launcher completed indicating end of use to display");
	}

	log_message("mobile launcher retrieving dekimberlize log file");
	if (retrieve_file_in_pieces(conn, "/tmp/dekimberlize.log") < 0) {
	    fprintf(stderr, "(mobile-launcher) Couldn't retrieve '/tmp/dekimberlize.log'\n");
	}
	else {
	    log_append_file("/tmp/dekimberlize.log");
	}

	log_message("mobile launcher retrieving dekimberlize log file");

	free_state(diff_filename, 1);
    }

    log_deinit();

    if(gerr) g_error_free (gerr);
    if(dbus_proxy) g_object_unref(dbus_proxy);

    exit(ret);
}
