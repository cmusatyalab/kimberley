#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "rpc_mobile_launcher.h"
#include "display_launcher.h"
#include "common.h"


static char  command[ARG_MAX];


void *
launch_display_scripts(void *arg) {
  int err;

  fprintf(stderr, "(display-launcher) Executing display script: %s\n",
	  command);
  err = pthread_mutex_lock(&current_state.mutex);
  if(err < 0) {
    fprintf(stderr, "(display-launcher) pthread_mutex_lock returned "
	    "error: %d\n", err);
    pthread_exit((void *)-1);
  }

  current_state.display_in_progress = 1;

  remove("/tmp/x11vnc_port");
  remove("/tmp/dekimberlize.resumed");

  err = system(command);
  if(err < 0) {
    perror("system");
    pthread_exit((void *)-1);
  }
  
  current_state.display_in_progress = 0;

  err = pthread_mutex_unlock(&current_state.mutex);
  if(err < 0) {
    fprintf(stderr, "(display-launcher) pthread_mutex_unlock returned "
	    "error: %d\n", err);
    pthread_exit((void *)-1);
  }

  fprintf(stderr, "(display-launcher) Display scripts completed.\n");

  pthread_exit((void *)0);
}


int
handle_dekimberlize_thread_setup() {
  int err, port, i;
  pthread_t tid;
  FILE *fp = NULL;
  char port_str[PATH_MAX];


  memset(&tid, 0, sizeof(pthread_t));
  err = pthread_create(&tid, NULL, launch_display_scripts, NULL);
  if(err != 0) {
    fprintf(stderr, "(display-launcher) failed creating thread\n");
    return -1;
  }


  /*
   * At this point, a new nested X server and a VNC server connected to it
   * will soon be created, listening for connections on a port specified
   * in /tmp/x11vnc_port.  dekimberlize is loading the vm in the background.
   */

  fprintf(stderr, "\nWaiting for thin client server to come up..");

  port_str[0]='\0';
  do {
    struct timeval tv;
    struct stat buf;
    int err;

    memset(&buf, 0, sizeof(struct stat));
    memset(&tv, 0, sizeof(struct timeval));

    err = stat("/tmp/x11vnc_port", &buf);
    if(!err && buf.st_size > 0) {
      fprintf(stderr, "(display-launcher) opened /tmp/x11vnc_port (size=%u)\n",
	      (unsigned int) buf.st_size);
      fp = fopen("/tmp/x11vnc_port", "r");
      if(fp != NULL)
	break;
      else
      	perror("fopen"); 
    }


    /*
     * Sleep a second, waiting for our thin client server to come up.
     */

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    select(0, NULL, NULL, NULL, &tv);

    fprintf(stderr, ".");
  }
  while(fp == NULL);

  fprintf(stderr, "(display-launcher) opened /tmp/x11vnc_port!\n");

  do {
    char *str = fgets(port_str, PATH_MAX, fp);
    struct timeval tv;
    
    if(str == NULL) 
      goto loop;
    
    if(strlen(str) > 5)
      break;
    
  loop:

    rewind(fp);
    
    fprintf(stderr, ".");
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    /* Sleep a second, then try again. */
    select(0, NULL, NULL, NULL, &tv);
  }
  while(1);

  fclose(fp);
  fp = NULL;

  for(i=0; i<10; i++)
    if(isdigit(port_str[i]))
       break;
  strcpy(port_str, &(port_str[i]));

  port = atoi(port_str);

  fprintf(stderr, "(display-launcher) Registering VNC port %u with Avahi\n",
	  port);

  if(create_dcm_service(VNC_DCM_SERVICE_NAME, port) < 0) {
    fprintf(stderr, "(display-launcher) failed creating "
	    "VNC service in DCM..\n");
    return -1;
  }


  fprintf(stderr, "(display-launcher) Waiting for VM to come up..\n");

  do {
    struct timeval tv;
    struct stat buf;
    int err;

    memset(&buf, 0, sizeof(struct stat));
    memset(&tv, 0, sizeof(struct timeval));

    err = stat("/tmp/dekimberlize.resumed", &buf);
    if(!err) {
      fp = fopen("/tmp/dekimberlize.resumed", "r");
      if(fp == NULL)
	perror("fopen");
      else
	fprintf(stderr, "(display-launcher) opened /tmp/dekimberlize.resumed\n", (unsigned int) buf.st_size);
    }

    /*
     * Sleep a millisecond, waiting for our VM to come up.
     */

    tv.tv_sec = 0;
    tv.tv_usec = 1000;

    select(0, NULL, NULL, NULL, &tv);
  }
  while(fp == NULL);

  fclose(fp);

  return 0;
}


bool_t
load_vm_from_path_1_svc(char *vm_name, char *patch_path, int *result, struct svc_req *rqstp)
{
  
  if((vm_name == NULL) || (patch_path == NULL) || (result == NULL)) {
    fprintf(stderr, "(display-launcher) Bad args to vm_path!\n");
    if(result)
      *result = -1;
    return FALSE;
  }

  fprintf(stderr, "(display-launcher) Preparing new VNC display with "
	  "vm '%s', kimberlize patch '%s'..\n", vm_name, patch_path);

  snprintf(command, ARG_MAX, "display_setup -f %s %s", patch_path, vm_name);

  *result = handle_dekimberlize_thread_setup();

  return TRUE;
}


bool_t
load_vm_from_url_1_svc(char *vm_name, char *patch_URL, int *result,  struct svc_req *rqstp)
{
  int err;
  char arg[PATH_MAX];
    
  if((vm_name == NULL) || (patch_URL == NULL) || (result == NULL)) {
    fprintf(stderr, "(display-launcher) Bad args to vm_path!\n");
    if(result)
      *result = -1;
    return FALSE;
  }

  fprintf(stderr, "(display-launcher) Preparing new VNC display with "
	  "vm '%s', kimberlize patch '%s'..\n", vm_name, patch_URL);

  err = pthread_mutex_lock(&current_state.mutex);
  if(err < 0) {
    fprintf(stderr, "(display-launcher) pthread_mutex_lock returned "
	    "error: %d\n", err);
    *result = -1;
    return FALSE;
  }

  strncpy(current_state.overlay_location, patch_URL, 2048);
  current_state.overlay_location[PATH_MAX-1] = '\0';

  strncpy(current_state.vm_name, vm_name, PATH_MAX);
  current_state.vm_name[PATH_MAX-1] = '\0';

  err = pthread_mutex_unlock(&current_state.mutex);
  if(err < 0) {
    fprintf(stderr, "(display-launcher) pthread_mutex_unlock returned "
	    "error: %d\n", err);
    *result = -1;
    return FALSE;
  }

  snprintf(command, ARG_MAX, "display_setup ");

  if(strlen(current_state.persistent_state_filename) > 0) {
    snprintf(arg, PATH_MAX, "-a %s ", current_state.persistent_state_filename);
    strncat(command, arg, PATH_MAX);
  }

  if(strlen(current_state.encryption_key_filename) > 0) {
    snprintf(arg, PATH_MAX, "-d %s ", current_state.encryption_key_filename);
    strncat(command, arg, PATH_MAX);
  }
  
  snprintf(arg, PATH_MAX, "-i %s ", current_state.overlay_location);
  strncat(command, arg, PATH_MAX);

  strncat(command, vm_name, PATH_MAX);

  *result = handle_dekimberlize_thread_setup();

  
  return TRUE;
}


bool_t
load_vm_from_attachment_1_svc(char *vm_name, char *patch_file, int *result,  struct svc_req *rqstp)
{
  int err;
  char *bname, *copy;
  char arg[PATH_MAX];
  
  if((vm_name == NULL) || (patch_file == NULL) || (result == NULL)) {
    fprintf(stderr, "(display-launcher) Bad args to vm_path!\n");
    *result = -1;
    return FALSE;
  }

  fprintf(stderr, "(display-launcher) Preparing new VNC display with "
	  "vm '%s', attached kimberlize patch '%s'..\n", vm_name, patch_file);

  copy = strdup(patch_file);
  bname = basename(copy);

  err = pthread_mutex_lock(&current_state.mutex);
  if(err < 0) {
    fprintf(stderr, "(display-launcher) pthread_mutex_lock returned "
	    "error: %d\n", err);
    *result = -1;
    free(copy);
    return FALSE;
  }

  snprintf(current_state.overlay_location, PATH_MAX, "/tmp/%s", bname);
  strncpy(current_state.vm_name, vm_name, PATH_MAX);
  current_state.vm_name[PATH_MAX-1] = '\0';

  err = pthread_mutex_unlock(&current_state.mutex);
  if(err < 0) {
    fprintf(stderr, "(display-launcher) pthread_mutex_unlock returned "
	    "error: %d\n", err);
    *result = -1;
    free(copy);
    return FALSE;
  }

  snprintf(command, ARG_MAX, "display_setup ");

  if(strlen(current_state.persistent_state_filename) > 0) {
    snprintf(arg, PATH_MAX, "-a %s ", current_state.persistent_state_filename);
    strncat(command, arg, PATH_MAX);
  }

  if(strlen(current_state.encryption_key_filename) > 0) {
    snprintf(arg, PATH_MAX, "-d %s ", current_state.encryption_key_filename);
    strncat(command, arg, PATH_MAX);
  }
  
  snprintf(arg, PATH_MAX, "-f %s ", current_state.overlay_location);
  strncat(command, arg, PATH_MAX);


  strncat(command, vm_name, PATH_MAX);

  free(copy);

  *result = handle_dekimberlize_thread_setup();

  return TRUE;
}


static FILE *write_attachment = NULL;
static int   write_attachment_size = 0;

bool_t
send_file_1_svc(char *filename, int size, int *result, struct svc_req *rqstp)
{
  char *bname, *copy;
  char local_filename[PATH_MAX];

  fprintf(stderr, "(display-launcher) Receiving file '%s' of size %d..\n", 
	  filename, size);

  copy = strdup(filename);
  bname = basename(copy);
  snprintf(local_filename, PATH_MAX, "/tmp/%s", bname);

  fprintf(stderr, "(display-launcher) Writing file '%s'\n", local_filename);

  write_attachment = fopen(local_filename, "w+");
  if(write_attachment == NULL) {
    perror("fopen");
    free(copy);
    *result = -1;
    return TRUE;
  }

  write_attachment_size = size;

  free(copy);
  *result = 0;

  return TRUE;
}


bool_t
send_partial_1_svc(data part, int *result,  struct svc_req *rqstp)
{
  int err;

  if((write_attachment_size <= 0) || (write_attachment == NULL)) {
    *result = -1;
    return TRUE;
  }

  err = fwrite(part.data_val, part.data_len, 1, write_attachment);
  if(err <= 0) {
    perror("fwrite");
    *result = -1;
    return TRUE;
  }

  write_attachment_size -= part.data_len;
  fprintf(stderr, ".");

  *result = 0;

  if(write_attachment_size < 0)
    *result = -1;
  
  if(write_attachment_size <= 0) {
    fclose(write_attachment);

    fprintf(stderr, "\n(display-launcher) File transfer complete!\n");

    write_attachment = NULL;
    write_attachment_size = 0;
  }
  
  return TRUE;
}


static FILE *read_attachment = NULL;
static int   read_attachment_size = 0;

bool_t
retrieve_file_1_svc(char *filename, int *result, struct svc_req *rqstp)
{
  char *bname, *copy;
  char localname[PATH_MAX];
  struct stat buf;
  int ret;

  fprintf(stderr, "(display-launcher) client requested %s\n", filename);

  copy = strdup(filename);
  bname = basename(copy);
  snprintf(localname, PATH_MAX, "/tmp/%s", bname);

  memset(&buf, 0, sizeof(struct stat));
  ret = stat(localname, &buf);
  if(ret < 0) {
    perror("stat");
    *result = -1;
    return TRUE;
  }

  read_attachment = fopen(localname, "r");
  if(read_attachment == NULL) {
    perror("fopen");
    *result = -1;
    return TRUE;
  }

  *result = buf.st_size;
  read_attachment_size = buf.st_size;

  fprintf(stderr, "(display-launcher) sending client file %s\n", localname);

  return TRUE;
}


bool_t
retrieve_partial_1_svc(data *result,  struct svc_req *rqstp)
{
  int bytes_read;
  char *partial_read;

  memset((char *)result, 0, sizeof(data));

  if((read_attachment == NULL) || (read_attachment_size <= 0))
    return FALSE;

  partial_read = (char *)malloc(CHUNK_SIZE);
  if(partial_read == NULL) {
    perror("malloc");
    return FALSE;
  }

  bytes_read = fread(partial_read, 1, CHUNK_SIZE, read_attachment);
  if(bytes_read <= 0) {
    if(feof(read_attachment)) {
      fprintf(stderr, "(display-launcher) end of file retrieval\n");
    }
    if(ferror(read_attachment)) {
      fprintf(stderr, "(display-launcher) error in file retrieval\n");
      perror("fread");
    }
    free(partial_read);
    return FALSE;
  }

  read_attachment_size -= bytes_read;
  fprintf(stderr, ".");

  if(read_attachment_size <= 0) {
    fclose(read_attachment);

    fprintf(stderr, "\n(display-launcher) Outgoing file transfer complete!\n");

    read_attachment = NULL;
    read_attachment_size = 0;
  }

  result->data_len = bytes_read;
  result->data_val = partial_read;
  
  return TRUE;
}


bool_t
end_usage_1_svc(int retrieve_state, char **result, struct svc_req *rqstp)
{
  int fd, i, err;
  char *filename;

  fd = open("/tmp/dekimberlize_finished", O_RDWR|O_CREAT);
  close(fd);

  fprintf(stderr, "(display-launcher) (**result)=%p, (*result)=%p\n",
	  result, *result);

  *result = (char *)malloc(PATH_MAX * sizeof(char));
  if(*result == NULL) {
    perror("malloc");
    return FALSE;
  }
  filename=*result;
  filename[0] = '\0';

  if(retrieve_state > 0) {
    int err;

    err = pthread_mutex_lock(&current_state.mutex);
    if(err < 0) {
      fprintf(stderr, "(display-launcher) pthread_mutex_lock returned "
	      "error: %d\n", err);
      return FALSE;
    }

    fprintf(stderr, "(display-launcher) copying %s into returned filename.\n",
	    current_state.persistent_state_diff_filename);
    
    strcpy(filename, current_state.persistent_state_diff_filename);
    
    err = pthread_mutex_unlock(&current_state.mutex);
    if(err < 0) {
      fprintf(stderr, "(display-launcher) pthread_mutex_unlock returned "
	      "error: %d\n", err);
      return FALSE;
    }
  }
  else {
    filename[0] = '\0';
  }

  fprintf(stderr, "(display-launcher) client told to retrieve state: %s\n",
	  *result);

  return TRUE;
}


bool_t
ping_1_svc(void *result, struct svc_req *rqstp)
{
  return TRUE;
}


bool_t
use_usb_cable_1_svc(int *result, struct svc_req *rqstp)
{
  return TRUE;
}


/*
 * This call specifies that a persistent state is ready to be
 * shipped over the wire, that will be attached to a running
 * virtual machine by the Dekimberlize process.  The system
 * then expects the next file to be sent to be this.
 */

bool_t
use_persistent_state_1_svc(char *filename, int *result,  struct svc_req *rqstp)
{
  int err;
  char *bname, *copy;
  char command[ARG_MAX];
  char local_filename[PATH_MAX];

  copy = strdup(filename);
  bname = basename(copy);
  snprintf(local_filename, PATH_MAX, "/tmp/%s", bname);


  err = pthread_mutex_lock(&current_state.mutex);
  if(err < 0) {
    fprintf(stderr, "(display-launcher) pthread_mutex_lock returned "
	    "error: %d\n", err);
    free(copy);
    *result = -1;
    return FALSE;
  }

  fprintf(stderr, "(display-launcher) Decompressing persistent state %s..\n",
	  local_filename);

  decompress_file(local_filename, current_state.persistent_state_filename);
  snprintf(current_state.persistent_state_modified_filename, PATH_MAX, 
	   "%s.new", current_state.persistent_state_filename);
  snprintf(current_state.persistent_state_diff_filename, PATH_MAX, 
	   "%s.diff", current_state.persistent_state_filename);

  err = pthread_mutex_unlock(&current_state.mutex);
  if(err < 0) {
    fprintf(stderr, "(display-launcher) pthread_mutex_unlock returned "
	    "error: %d\n", err);
    *result = -1;
    free(copy);
    return FALSE;
  }

  fprintf(stderr, "(display-launcher) Using persistent state:\n"
	  "\t original file: %s\n"
	  "\t modified file: %s\n"
	  "\t binary difference file: %s\n",
	  current_state.persistent_state_filename,
	  current_state.persistent_state_modified_filename,
	  current_state.persistent_state_diff_filename);

  free(copy);
  *result = 0;

  return TRUE;
}


/*
 * This call specifies that a persistent state is ready to be
 * shipped over the wire, that will be attached to a running
 * virtual machine by the Dekimberlize process.  The system
 * then expects the next file to be sent to be this.
 */

bool_t
use_encryption_key_1_svc(char *filename, int *result,  struct svc_req *rqstp)
{
  int err;
  char *bname, *copy;

  copy = strdup(filename);
  bname = basename(copy);

  err = pthread_mutex_lock(&current_state.mutex);
  if(err < 0) {
    fprintf(stderr, "(display-launcher) pthread_mutex_lock returned "
	    "error: %d\n", err);
    free(copy);
    *result = -1;
    return FALSE;
  }
  
  snprintf(current_state.encryption_key_filename, PATH_MAX, 
	   "/tmp/%s", bname);

  err = pthread_mutex_unlock(&current_state.mutex);
  if(err < 0) {
    fprintf(stderr, "(display-launcher) pthread_mutex_unlock returned "
	    "error: %d\n", err);
    *result = -1;
    free(copy);
    return FALSE;
  }

  fprintf(stderr, "(display-launcher) Using encryption key: %s\n",
	  current_state.encryption_key_filename);

  free(copy);
  *result = 0;

  return TRUE;
}


int
mobilelauncher_prog_1_freeresult(SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
  xdr_free (xdr_result, result);
  return 1;
}
