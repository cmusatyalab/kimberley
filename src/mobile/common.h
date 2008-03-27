#ifndef _COMMON_H_
#define _COMMON_H_

#include <rpc/rpc.h>

#define DCM_DBUS_SERVICE_NAME		"edu.cmu.cs.diamond.opendiamond.dcm"
#define DCM_DBUS_SERVICE_PATH		"/edu/cmu/cs/diamond/opendiamond/dcm"
#define LAUNCHER_DCM_SERVICE_NAME	"_launcher_dcm._tcp"
#define VNC_DCM_SERVICE_NAME		"_vnc_dcm._tcp"


/*
 * The chunk size is 1 megabyte for sending parts of files between
 * client and server.
 */

#define CHUNK_SIZE 1048576


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
