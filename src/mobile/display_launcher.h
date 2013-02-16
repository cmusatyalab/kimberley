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
