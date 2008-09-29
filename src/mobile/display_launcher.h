#ifndef _MOBILE_LAUNCHER_H_
#define _MOBILE_LAUNCHER_H_

typedef struct {
  pthread_mutex_t mutex;
  int display_in_progress;
  char vm_name[PATH_MAX];
  char overlay_location[PATH_MAX];
  char encryption_key_filename[PATH_MAX];
  char persistent_state_filename[PATH_MAX];
  char persistent_state_modified_filename[PATH_MAX];
  char persistent_state_diff_filename[PATH_MAX];
} kimberley_state_t;

extern volatile kimberley_state_t current_state;

int		make_tcpip_connection(char *hostname, unsigned short port);
void		local_tunnel(int kcm_sock, int rpc_sock);
int		create_kcm_service(char *name, unsigned short port);

#endif
