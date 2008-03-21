#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "rpc_mobile_launcher.h"
#include "display_launcher.h"
#include "common.h"


static char command[ARG_MAX];


void *
launch_display_scripts(void *arg) {
  int err;

  fprintf(stderr, "(launcher-rpc-server) executing display scripts..\n");

  err = system(command);
  if(err < 0) {
    perror("system");
    pthread_exit((void *)-1);
  }
  
  fprintf(stderr, "(launcher-rpc-server) display scripts completed.\n");

  pthread_exit((void *)0);
}


int
handle_dekimberlize_thread_setup() {
  int err, port, i;
  pthread_t tid;
  FILE *fp;
  char port_str[MAXPATHLEN];


  bzero(&tid, sizeof(pthread_t));
  err = pthread_create(&tid, NULL, launch_display_scripts, NULL);
  if(err != 0) {
    fprintf(stderr, "(launcher-rpc-server) failed creating thread\n");
    return -1;
  }


  /* At this point, a new nested X server and a VNC server connected to it
   * will soon be created, listening for connections on a port specified
   * in /tmp/x11vnc_port.  dekimberlize is loading the vm in the background. */

  port_str[0]='\0';
  do {
    struct timeval tv;
    fp = fopen("/tmp/x11vnc_port", "r+");

    if(fp == NULL)
      if(errno != ENOENT) {
	perror("fopen");
	pthread_exit((void *)-1);
      }

    fprintf(stderr, ".");
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    /* Sleep a second, then try again. */
    select(0, NULL, NULL, NULL, &tv);
  }
  while(fp == NULL);

  fprintf(stderr, "(launcher-rpc-server) opened /tmp/x11vnc_port!\n");

  do {
    char *str = fgets(port_str, MAXPATHLEN, fp);
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

  for(i=0; i<10; i++)
    if(isdigit(port_str[i]))
       break;
  strcpy(port_str, &(port_str[i]));

  port = atoi(port_str);

  fprintf(stderr, "(launcher-rpc-server) Registering VNC port %u with Avahi\n",
	  port);

  if(create_dcm_service(VNC_DCM_SERVICE_NAME, port) < 0) {
    fprintf(stderr, "(launcher-rpc-server) failed creating "
	    "VNC service in DCM..\n");
    return -1;
  }

  return 0;
}


bool_t
load_vm_from_path_1_svc(char *vm_name, char *patch_path, int *result, struct svc_req *rqstp)
{
  
  if((vm_name == NULL) || (patch_path == NULL) || (result == NULL)) {
    fprintf(stderr, "(launcher-rpc-server) Bad args to vm_path!\n");
    if(result)
      *result = -1;
    return FALSE;
  }

  fprintf(stderr, "(launcher-rpc-server) Preparing new VNC display with "
	  "vm '%s', kimberlize patch '%s'..\n", vm_name, patch_path);

  snprintf(command, ARG_MAX, "display_setup -f %s %s", patch_path, vm_name);

  *result = handle_dekimberlize_thread_setup();

  return TRUE;
}


bool_t
load_vm_from_url_1_svc(char *vm_name, char *patch_URL, int *result,  struct svc_req *rqstp)
{
    
  if((vm_name == NULL) || (patch_URL == NULL) || (result == NULL)) {
    fprintf(stderr, "(launcher-rpc-server) Bad args to vm_path!\n");
    if(result)
      *result = -1;
    return FALSE;
  }

  fprintf(stderr, "(launcher-rpc-server) Preparing new VNC display with "
	  "vm '%s', kimberlize patch '%s'..\n", vm_name, patch_URL);

  snprintf(command, ARG_MAX, "display_setup -i %s %s", patch_URL, vm_name);

  *result = handle_dekimberlize_thread_setup();

  
  return TRUE;
}


bool_t
load_vm_from_attachment_1_svc(char *vm_name, data patch_data, int *result,  struct svc_req *rqstp)
{

  fprintf(stderr, "(launcher-rpc-server) vm_attached not implemented!\n");

  return TRUE;
}


bool_t
end_usage_1_svc(int retrieve_state, void *result,  struct svc_req *rqstp)
{
  int fd;

  fd = open("/tmp/dekimberlize_finished", O_RDWR|O_CREAT);
  close(fd);
  
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

bool_t
send_persistent_state_1_svc(data patch, int *result,  struct svc_req *rqstp)
{
  return TRUE;
}

bool_t
retrieve_persistent_state_1_svc(data *result, struct svc_req *rqstp)
{
  return TRUE;
}

int
mobilelauncher_prog_1_freeresult(SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
  xdr_free (xdr_result, result);
  return 1;
}
