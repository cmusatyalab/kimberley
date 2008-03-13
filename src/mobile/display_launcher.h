#ifndef _MOBILE_LAUNCHER_H_
#define _MOBILE_LAUNCHER_H_

int		make_tcpip_connection(char *hostname, unsigned short port);
void		local_tunnel(int dcm_sock, int rpc_sock);
int		create_dcm_service(char *name, unsigned short port);

#endif
