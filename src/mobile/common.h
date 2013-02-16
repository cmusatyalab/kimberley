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

#ifndef _COMMON_H_
#define _COMMON_H_

#include <rpc/rpc.h>

#define ARG_MAX 255

#define KCM_DBUS_SERVICE_NAME		"edu.cmu.cs.kimberley.kcm"
#define KCM_DBUS_SERVICE_PATH		"/edu/cmu/cs/kimberley/kcm"
#define LAUNCHER_KCM_SERVICE_NAME	"_launcher_kcm._tcp"
#define VNC_KCM_SERVICE_NAME		"_vnc_kcm._tcp"


/*
 * The chunk size is 1 megabyte for sending parts of files between
 * client and server.
 */

#define CHUNK_SIZE 1048576

int            log_init(void);
int            log_message(char *message);
int            log_append_file(char *filename);
void           log_deinit(void);

int            compress_file(char *filename, char *new_filename);
int            decompress_file(char *filename, char *new_filename);

CLIENT *       convert_socket_to_rpc_client(int connfd, 
					    unsigned int prog,
					    unsigned int vers);

unsigned short choose_random_port(void);
unsigned short setup_rpc_server(unsigned int prog, 
                                unsigned int vers,
                                void (*handler)(struct svc_req *, SVCXPRT *),
				unsigned int conn_type);
unsigned short setup_rpc_server_with_port(unsigned int prog, 
                                          unsigned int vers,
                                          void (*handler)(struct svc_req *, 
                                                          SVCXPRT *),
                                          unsigned int conn_type,
                                          unsigned short port);
int            setup_rpc_server_and_connection(unsigned int prog, 
                                               unsigned int vers,
                                               void (*handler)(struct svc_req *, SVCXPRT *));


void           mobilelauncher_prog_1(struct svc_req *rqstp, 
                                     register SVCXPRT *transp);


#endif
