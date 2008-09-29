#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <minirpc/minirpc.h>
#include "rpc_mobile_launcher_server.h"

#include "display_launcher.h"
#include "common.h"

#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#define LOCALFILE_DIR "/tmp/"

static pid_t start_display_setup(kimberley_state_t *state)
{
    pid_t pid;
    char *argv[20];
    int n = 0;

    fprintf(stderr, "(display-launcher) starting display script\n");

    /* wheee, double fork */
    pid = fork();
    if (pid) {
	if (pid == -1)
	    perror("fork");
	waitpid(pid, NULL, 0);
	return pid;
    }
    pid = fork();
    if (pid) {
	if (pid == -1)
	     exit(EXIT_FAILURE);
	else exit(EXIT_SUCCESS);
    }

    argv[n++] = "display_setup";
    if (state->persistent_state) {
	argv[n++] = "-a";
	argv[n++] = state->persistent_state;
    }
    if (state->encryption_key) {
	argv[n++] = "-d";
	argv[n++] = state->encryption_key;
    }
    if (state->patch_file) {
	argv[n++] = "-f";
	argv[n++] = state->patch_file;
    }
    else if (state->overlay_location) {
	argv[n++] = "-i";
	argv[n++] = state->overlay_location;
    }
    argv[n++] = state->vm_name;
    argv[n++] = NULL;

    if (execvp(argv[0], argv))
    /* we really shouldn't get here... */
	perror("execvp");

    fprintf(stderr, "(display-launcher) Failed to start display script\n");
    exit(EXIT_FAILURE);
}

static int handle_dekimberlize_thread_setup(kimberley_state_t *state)
{
    char buf[256], *svc, *end;
    ssize_t n;
    pid_t pid;
    int fd;

    remove("/tmp/x11vnc_port");
    remove("/tmp/dekimberlize.resumed");

    pid = start_display_setup(state);
    if (pid == -1) return ENOEXEC;

    /*
     * At this point, a new nested X server and a VNC server connected to it
     * will soon be created, listening for connections on a port specified
     * in /tmp/x11vnc_port.  dekimberlize is loading the vm in the background.
     */

    fprintf(stderr, "(display-launcher) Waiting for thin client server..");

    while (1) {
	struct timeval tv = { .tv_sec = 1 };

	fd = open("/tmp/x11vnc_port", O_RDONLY);
	if (fd != -1) break;

	fprintf(stderr, ".");
	select(0, NULL, NULL, NULL, &tv);
    }

    fprintf(stderr, "\n(display-launcher) opened /tmp/x11vnc_port!\n");

    n = read(fd, buf, sizeof(buf)-1);
    if (n == -1) {
	perror("reading x11vnc_port");
	close(fd);
	return EIO;
    }
    buf[sizeof(buf)-1] = '\0';

    close(fd);

    /* find port number */
    for (svc = buf; *svc; svc++)
	if (isdigit(*svc)) break;
    for (end = svc; *end; end++)
	if (!isdigit(*end)) break;
    *end = '\0';

    if (!*svc) {
	fprintf(stderr, "(display-launcher) Failed to read VNC port number\n");
	return -1;
    }

	
    fprintf(stderr, "(display-launcher) Registering VNC port %s with Avahi\n",
	    svc);

    if (create_kcm_service(VNC_KCM_SERVICE_NAME, svc) < 0) {
	fprintf(stderr, "(display-launcher) failed creating "
		"VNC service in KCM..\n");
	return -1;
    }

    fprintf(stderr, "(display-launcher) Waiting for VM to come up..\n");

    while (1) {
	struct timeval tv = { .tv_usec = 1000 };

	if (access("/tmp/dekimberlize.resumed", F_OK) == 0)
	    break;

	fprintf(stderr, ".");
	select(0, NULL, NULL, NULL, &tv);
    }

    fprintf(stderr, "\n(display-launcher) found /tmp/dekimberlize.resumed\n");
    return 0;
}

static char *local_filename(const char *file)
{
    char *copy, *base, *name;

    copy = strdup(file);
    if (!copy) return NULL;

    base = basename(copy);
    name = malloc(strlen(LOCALFILE_DIR) + strlen(base) + 1);
    if (name)
	sprintf(name, LOCALFILE_DIR "%s", base);

    free(copy);
    return name;
}

static mrpc_status_t
load_vm_from_path(void *conn_data, struct mrpc_message *msg, load_vm_in *in)
{
    kimberley_state_t *state = conn_data;

    if (!in->vm_name || !in->uri) {
	fprintf(stderr, "(display-launcher) Bad args to vm_path!\n");
	return EINVAL;
    }

    fprintf(stderr, "(display-launcher) Preparing new VNC display with "
	    "vm '%s', kimberlize patch '%s'..\n", in->vm_name, in->uri);

    state->vm_name = strdup(in->vm_name);
    state->patch_file = strdup(in->uri);

//snprintf(command, ARG_MAX, "display_setup -f %s %s", in->uri, in->vm_name);

    return handle_dekimberlize_thread_setup(state);
}


static mrpc_status_t
load_vm_from_url(void *conn_data, struct mrpc_message *msg, load_vm_in *in)
{
    kimberley_state_t *state = conn_data;
    
    if (!in->vm_name || !in->uri) {
	fprintf(stderr, "(display-launcher) Bad args to vm_path!\n");
	return EINVAL;
    }

    fprintf(stderr, "(display-launcher) Preparing new VNC display with "
	    "vm '%s', kimberlize patch '%s'..\n", in->vm_name, in->uri);

    state->vm_name = strdup(in->vm_name);
    state->overlay_location = strdup(in->uri);

    return handle_dekimberlize_thread_setup(state);
}

static mrpc_status_t
load_vm_from_attachment(void *conn_data,struct mrpc_message *msg,load_vm_in *in)
{
    kimberley_state_t *state = conn_data;

    if (!in->vm_name || !in->uri) {
	fprintf(stderr, "(display-launcher) Bad args to vm_path!\n");
	return EINVAL;
    }

    fprintf(stderr, "(display-launcher) Preparing new VNC display with "
	    "vm '%s', attached kimberlize patch '%s'.\n", in->vm_name, in->uri);

    state->vm_name = strdup(in->vm_name);
    state->patch_file = local_filename(in->uri);

    return handle_dekimberlize_thread_setup(state);
}

static mrpc_status_t
send_chunk(void *conn_data, struct mrpc_message *msg, send_x *in)
{
    char *localname;
    int fd = -1;
    int err = ENOMEM;
    ssize_t n;

    localname = local_filename(in->name);
    if (!localname) {
	perror("send local_filename");
	goto err_out;
    }

    fd = open(localname, O_WRONLY|O_CREAT|O_NOFOLLOW, S_IRUSR|S_IWUSR);
    if (fd == -1) {
	err = errno;
	perror("send open");
	goto err_out;
    }

    n = pwrite(fd, in->data.data_val, in->data.data_len, in->offset);
    if (n < 0) {
	err = errno;
	perror("send pwrite");
	goto err_out;
    }
    err = 0;

err_out:
    if (fd != -1) close(fd);
    if (localname) free(localname);
    return err;
}


static mrpc_status_t
retrieve_chunk(void *conn_data, struct mrpc_message *msg,
	       retrieve_x *in, data *out)
{
    char *localname;
    int fd = -1;
    int err = ENOMEM;
    ssize_t n;

    localname = local_filename(in->name);
    if (!localname) {
	perror("retrieve local_filename");
	goto err_out;
    }

    fd = open(localname, O_RDONLY);
    if (fd == -1) {
	err = errno;
	perror("retrieve open");
	goto err_out;
    }

    out->data_val = malloc(CHUNK_SIZE);
    if (!out->data_val) {
	err = errno;
	perror("retrieve malloc");
	goto err_out;
    }

    n = pread(fd, out->data_val, CHUNK_SIZE, in->offset);
    if (n < 0) {
	err = errno;
	perror("retrieve pread");
	goto err_out;
    }

    if (n == 0)
	fprintf(stderr, "(display-launcher) end of file retrieval\n");

    out->data_len = n;
    err = 0;

err_out:
    if (fd != -1) close(fd);
    if (localname) free(localname);
    return err;
}

static mrpc_status_t
ping(void *conn_data, struct mrpc_message *msg)
{
    return 0;
}

static mrpc_status_t
use_usb_cable(void *conn_data, struct mrpc_message *msg)
{
    return 0;
}


/*
 * This call specifies that a persistent state is ready to be
 * shipped over the wire, that will be attached to a running
 * virtual machine by the Dekimberlize process.  The system
 * then expects the next file to be sent to be this.
 */
static mrpc_status_t
use_persistent_state(void *conn_data, struct mrpc_message *msg, filename *in)
{
    kimberley_state_t *state = conn_data;
    char *localname;

    localname = local_filename(*in);
    if (!localname) return 1;

    fprintf(stderr, "(display-launcher) Decompressing persistent state %s..\n",
	    localname);

    state->persistent_state = decompress_file(localname);
    free(localname);

    if (!state->persistent_state) {
	fprintf(stderr, "(display-launcher) Failed decompression\n");
	return 1;
    }

    fprintf(stderr, "(display-launcher) Using persistent state: %s\n",
	    state->persistent_state);
    return 0;
}

/*
 * This call specifies that an encryption key is ready to be
 * shipped over the wire, that will be attached to a running
 * virtual machine by the Dekimberlize process.  The system
 * then expects the next file to be sent to be this.
 */
static mrpc_status_t
use_encryption_key(void *conn_data, struct mrpc_message *msg, filename *in)
{
    kimberley_state_t *state = conn_data;

    state->encryption_key = local_filename(*in);
    if (!state->encryption_key) return 1;

    fprintf(stderr, "(display-launcher) Using encryption key file: %s\n",
	    state->encryption_key);
    return 0;
}

static mrpc_status_t
end_usage(void *conn_data, struct mrpc_message *msg,
	  retrieve_state *in, state *out)
{
    kimberley_state_t *state = conn_data;
    int fd;

    fd = open("/tmp/dekimberlize_finished", O_WRONLY|O_CREAT|O_NOFOLLOW,
	      S_IRUSR|S_IWUSR);
    close(fd);

    *out = malloc(strlen(state->persistent_state) + 6);

    if (in) {
	sprintf(*out, "%s.diff", state->persistent_state);
	fprintf(stderr,"(display-launcher) client told to retrieve state: %s\n",
		*out);
    }
    else (*out)[0] = '\0';

    return 0;
}


static const struct rpc_mobile_launcher_server_operations ops = {
    .load_vm_from_URL = load_vm_from_url,
    .load_vm_from_path = load_vm_from_path,
    .load_vm_from_attachment = load_vm_from_attachment,
    .send_chunk = send_chunk,
    .retrieve_chunk = retrieve_chunk,
    .ping = ping,
    .use_USB_cable = use_usb_cable,
    .use_persistent_state = use_persistent_state,
    .use_encryption_key = use_encryption_key,
    .end_usage = end_usage,
};
const struct rpc_mobile_launcher_server_operations *server_ops = &ops;

