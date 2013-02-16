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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <rpc/pmap_clnt.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common.h"


static int log_ready = 0;
static FILE *log_fp = NULL;
static pthread_mutex_t log_mutex;


int
log_init(void) {
  int err;
  struct timeval tv;
  struct tm* tm;
  char log_filename[PATH_MAX];
  char time_str[200];


  if(log_ready != 0 || log_fp != NULL)
    return -1;
  
  memset(&tv, 0, sizeof(struct timeval));

  err = pthread_mutex_init(&log_mutex, NULL);
  if(err != 0) {
    fprintf(stderr, "(common) failed initializing log mutex!\n");
    return -1;
  }

  gettimeofday(&tv, NULL);
  tm = localtime(&tv.tv_sec);


  /*
   * Format the date and time, down to a single second. 
   */

  strftime(time_str, 200, "%Y-%m-%d_%H:%M:%S", tm);


  /*
   * Print the formatted time, in seconds, followed by a decimal point
   *  and the milliseconds. 
   */

  snprintf(log_filename, PATH_MAX, "/tmp/%s.log", time_str);
  fprintf(stderr, "(common) initializing log: %s\n", log_filename);

  log_fp = fopen(log_filename, "w+");
  if(log_fp == NULL) {
    perror("(common) fopen");
    return -1;
  }
  
  log_ready = 1;

  return 0;
}


int
log_message(char *message) {
  int err;
  struct timeval tv;
  struct tm *tm;
  char ftime_str[200];

  if(log_ready == 0 || log_fp == NULL || message == NULL)
    return -1;

  err = pthread_mutex_lock(&log_mutex);
  if(err < 0) {
    fprintf(stderr, "(common) pthread_mutex_lock returned "
	    "error: %d\n", err);
    return -1;
  }

  memset(&tv, 0, sizeof(struct timeval));
  gettimeofday(&tv, NULL);

  tm = localtime(&tv.tv_sec);


  /*
   * Format the date and time, down to a single second. 
   */

  strftime(ftime_str, 200, "%Y-%m-%d_%H:%M:%S", tm);
  
  err = fprintf(log_fp, "%s.%.6u: %s\n", ftime_str, tv.tv_usec, message);
  if(err <= 0) {
    perror("(common) fwrite");
    pthread_mutex_unlock(&log_mutex);
    return -1;
  }

  fflush(log_fp);

  err = pthread_mutex_unlock(&log_mutex);
  if(err < 0) {
    fprintf(stderr, "(common) pthread_mutex_unlock returned "
	    "error: %d\n", err);
    return -1;
  }

  return 0;
}

int
log_append_file(char *filename) {
  int bytes, err;
  struct stat buf;
  FILE *fp;

  if(log_ready == 0 || log_fp == NULL || filename == NULL)
    return -1;

  memset(&buf, 0, sizeof(struct stat));
  err = stat(filename, &buf);
  if(err < 0) {
    fprintf(stderr, "(common) couldn't stat log file %s\n", filename);
    return -1;
  }

  bytes = buf.st_size;
  fp = fopen(filename, "r");
  if(fp == NULL) {
    perror("(common) fopen");
    return -1;
  }

  err = pthread_mutex_lock(&log_mutex);
  if(err < 0) {
    fprintf(stderr, "(common) pthread_mutex_lock returned "
	    "error: %d\n", err);
    fclose(fp);
    return -1;
  }

  while(bytes > 0) {
    int num_read;
    char str[ARG_MAX];
    
    num_read = fread(str, 1, ARG_MAX, fp);
    if(num_read <= 0) 
      continue;

    fwrite(str, num_read, 1, log_fp);

    bytes-=num_read;
  }

  fflush(log_fp);

  err = pthread_mutex_unlock(&log_mutex);
  if(err < 0) {
    fprintf(stderr, "(common) pthread_mutex_unlock returned "
	    "error: %d\n", err);
    fclose(fp);
    return -1;
  }

  fclose(fp);

  return 0;
}

void
log_deinit(void) {
  int err;

  err = pthread_mutex_lock(&log_mutex);
  if(err < 0)
    fprintf(stderr, "(common) pthread_mutex_lock returned "
	    "error: %d\n", err);

  fprintf(stderr, "(common) deinitializing log\n");

  log_ready = 0;
  if(log_fp != NULL) {
    fclose(log_fp);
    log_fp = NULL;
  }

  err = pthread_mutex_unlock(&log_mutex);
  if(err < 0)
    fprintf(stderr, "(common) pthread_mutex_unlock returned "
	    "error: %d\n", err);
}


/*
 * Write "n" bytes to a descriptor reliably. 
 */

ssize_t                        
writen(int fd, const void *vptr, size_t n)
{
  size_t nleft;
  ssize_t nwritten;
  const char *ptr;

  ptr = vptr;
  nleft = n;
  while (nleft > 0) {
    if ( (nwritten = write(fd, ptr, nleft)) <= 0) {
      if (nwritten < 0 && errno == EINTR)
        nwritten = 0;   /* and call write() again */
      else
        return (-1);    /* error */
    }

    nleft -= nwritten;
    ptr += nwritten;
  }
  return (n);
}


unsigned short
choose_random_port(void) {
  struct timeval t;
  unsigned short port;

  /* Choose a random TCP port between 10k and 65k to allow a client to
   * connect on.  Use high-order bits for improved randomness. */

  gettimeofday(&t, NULL);
  srand(t.tv_usec);
  port = 10000 + (int)(55000.0 * (rand()/(RAND_MAX + 1.0)));

  return port;
}


int
make_tcpip_connection(char *hostname, unsigned short port) {
  int sockfd, err;
  char port_str[NI_MAXSERV];
  struct addrinfo *info, hints;

  if((hostname == NULL) || (port == 0))
    return -1;

  if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    return -1;
  }

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_flags = AI_CANONNAME;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(port_str, 6, "%u", port);

  if((err = getaddrinfo(hostname, port_str, &hints, &info)) < 0) {
    fprintf(stderr, "(launcher) getaddrinfo failed: %s\n", gai_strerror(err));
    return -1;
  }

  if(connect(sockfd, info->ai_addr, sizeof(struct sockaddr_in)) < 0) {
    perror("connect");
    return -1;
  }

  freeaddrinfo(info);

  return sockfd;
}


void
local_tunnel(int sock1, int sock2) {

  fprintf(stderr, "Tunneling between fd=%d and fd=%d\n", sock1, sock2);
  
  while(1) {
    int size_in, size_out, in, out, maxfd, err;
    char buf[4096], *dir;
    fd_set readfds, exceptfds;
    
    FD_ZERO(&readfds);
    FD_ZERO(&exceptfds);
    FD_SET(sock1, &readfds);
    FD_SET(sock2, &readfds);
    FD_SET(sock1, &exceptfds);
    FD_SET(sock2, &exceptfds);

    maxfd = sock1;
    if(sock2 > maxfd)
      maxfd = sock2;
    maxfd++;

    err = select(maxfd, &readfds, NULL, &exceptfds, NULL);
    if(err < 0) {
      perror("select");
      close(sock1);
      close(sock2);
      pthread_exit((void *)-1);
    }


    /* Figure out which direction information will flow. */
    if(FD_ISSET(sock1, &exceptfds) || FD_ISSET(sock2, &exceptfds)) {
      fprintf(stderr, "(launcher-tunnel) select() reported exceptions!\n");
      close(sock1);
      close(sock2);
      pthread_exit((void *)-1);
    }
  
  
    if(FD_ISSET(sock1, &readfds)) {
      in = sock1;
      out = sock2;
      dir = "out";
    }
    else if(FD_ISSET(sock2, &readfds)) {
      in = sock2;
      out = sock1;
      dir = "in";
    }
    else {
      fprintf(stderr, "(launcher-tunnel) select() succeeded, but no file "
              "descriptors are ready to be read!\n");
      close(sock1);
      close(sock2);
      pthread_exit((void *)-1);
    }
    
    
    /* Pass up to 4096 bytes of data between sockets. */
    
    size_in = read(in, (void *)buf, 4096);
    if(size_in < 0) {
      perror("read");
      continue;
    }
    else if(size_in == 0) { /* EOF */
      fprintf(stderr, "(launcher-tunnel) A connection (%d) was closed "
	      "(EOF on read).\n", in);
      close(sock1);
      close(sock2);
      pthread_exit((void *)0);
    }

    size_out = writen(out, (void *)buf, size_in);
    if(size_out < 0) {
      perror("write");
      fprintf(stderr, "(launcher-tunnel) A connection (%d) was closed "
	      "(EOF on write).\n", out);
      close(sock1);
      close(sock2);
      pthread_exit((void *)0);
    }

    if(size_in != size_out) {
      fprintf(stderr, "(launcher-tunnel) Somehow lost bytes, "
	      "from %d in to %d out!\n", size_in, size_out);
      continue;
    }
    
    //    fprintf(stderr, "(launcher-tunnel) tunneled %d bytes %s\n", size_in, dir);
  }

  /* Should never get here. */ 
 
  pthread_exit((void *)0);
}


/* 
 * Spawn an RPC server in a new thread and return the port number on which
 * it can be reached at. Does not form a loopback connection for tunneling. 
 */

typedef struct {
  uint16_t volatile *control_ready;
  uint16_t port;
  unsigned int prog;
  unsigned int vers;
  unsigned int connection_type;
  void (*handler)(struct svc_req *, SVCXPRT *);
} crs_args_t;


static void *
rpc_server_thread(void *arg) {
    SVCXPRT *transp;
    struct sockaddr_in servaddr;
    int rpcfd;

    crs_args_t *args = (crs_args_t *)arg;

    if(args == NULL) {
      fprintf(stderr, "(libsstub) rpc_server_thread: NULL arguments passed\n");
      pthread_exit((void *)-1);
    }

    if((rpcfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      perror("socket");
      pthread_exit((void *)-1);
    }
  
    memset(&servaddr, 0, sizeof(struct sockaddr_in));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(args->connection_type);
    servaddr.sin_port = htons(args->port);

    if(bind(rpcfd, (struct sockaddr *) &servaddr, 
	    sizeof(struct sockaddr_in)) < 0) {
      perror("bind");
      pthread_exit((void *)-1);
    }

    transp = svctcp_create(rpcfd, BUFSIZ, BUFSIZ);
    if (transp == NULL) {
      fprintf (stderr, "%s", "cannot create Sun RPC tcp service.");
      pthread_exit((void *)-1);
    }

    pmap_unset(args->prog, args->vers);
    
    if (!svc_register(transp, args->prog, args->vers, args->handler, 0)) {
      fprintf(stderr, "(Sun RPC) unable to register \"client to "
	      "content\" program (prog=0x%x, vers=%d, tcp)\n",  
	      args->prog, args->vers);
      pthread_exit((void *)-1);
    }

    *(args->control_ready) = 1;       /* Signal the parent thread that
				       * our Sun RPC server is ready to
				       * accept connections. */

    svc_run();
    pthread_exit((void *)-1);
}


unsigned short
setup_rpc_server_with_port(unsigned int prog, unsigned int vers,
			   void (*handler)(struct svc_req *, SVCXPRT *),
			   unsigned int conn_type, unsigned short port) {
    pthread_t rpc_thread;
    struct timeval t;

    volatile uint16_t control_ready = 0;
    crs_args_t *args;

    args = (crs_args_t *)calloc(1, sizeof(crs_args_t));
    
    
    gettimeofday(&t, NULL);
    srand(t.tv_usec);
    args->port = port;
    args->prog = prog;
    args->vers = vers;
    args->handler = handler;
    args->connection_type = conn_type;


    /* Set up a shared variable so the child can signal when it is
     * ready to receive incoming connections.  This variable is volatile
     * to force the compiler to check its value rather than optimize. */

    args->control_ready = &control_ready;

    /* Create a thread which becomes a Sun RPC server and
     * wait for it to come up and bind to the port. */

    memset(&rpc_thread, 0, sizeof(pthread_t));
    pthread_create(&rpc_thread, NULL, rpc_server_thread, (void *)args);
    
    while(control_ready == 0) 
      continue;

    return port;
}


unsigned short
setup_rpc_server(unsigned int prog, unsigned int vers,
		 void (*handler)(struct svc_req *, SVCXPRT *),
		 unsigned int conn_type) {

  unsigned short port;
  struct timeval t;


  /* Choose a random TCP port between 10k and 65k to connect to the 
   * Sun RPC server on.  Use high-order bits for improved randomness. */

  gettimeofday(&t, NULL);
  srand(t.tv_usec);
  port = (unsigned short) (10000 + (int)(55000.0 * 
					 (rand()/(RAND_MAX + 1.0))));

  return setup_rpc_server_with_port(prog, vers, handler, conn_type, port);
}


/* 
 * This function spawns a thread which becomes a Sun RPC server, and then
 * makes a local TCP connection to that server.  It returns the socket file
 * descriptor to which a tunnel can write control bits.
 */

int
setup_rpc_server_and_connection(unsigned int prog, unsigned int vers,
				void (*handler)(struct svc_req *, SVCXPRT *)) {
  int connfd, port, error;
  char port_str[NI_MAXSERV];
  struct addrinfo *info, hints;
  
  
  /* If we're making the connection also, always use LOOPBACK to avoid
   * needlessly exposing the server to the outside world. */
  
  port = setup_rpc_server(prog, vers, handler, INADDR_LOOPBACK);
  
  /* Create new connection to the Sun RPC server. */
  if((connfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    pthread_exit((void *)-1);
  }
  
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_flags = AI_CANONNAME;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(port_str, NI_MAXSERV, "%u", port);
  
  if((error = getaddrinfo("localhost", port_str, &hints, &info)) < 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
    pthread_exit((void *)-1);
  }
  
  if(connect(connfd, info->ai_addr, sizeof(struct sockaddr_in)) < 0) {
    perror("sunrpc connect");
    pthread_exit((void *)-1);
  }
  
  freeaddrinfo(info);
  
  return connfd;
}


/* rpc_init() takes an already-connected socket file descriptor and
 * makes it into a TS-RPC (Sun RPC) client handle. */

CLIENT *
convert_socket_to_rpc_client(int connfd, unsigned int prog, 
			     unsigned int vers) {
  struct sockaddr_in control_name;
  struct timeval tv;
  unsigned int control_name_len = sizeof(struct sockaddr);
  CLIENT *clnt;
  
  if(getsockname(connfd, (struct sockaddr *)&control_name, 
		 &control_name_len) < 0) {
    perror("getsockname");
    return NULL;
  }
  
  if ((clnt = clnttcp_create(&control_name, prog, vers,
			     &connfd, BUFSIZ, BUFSIZ)) == NULL) {
    clnt_pcreateerror("clnttcp_create");
    return NULL;
  }
  
  tv.tv_sec = 30*60; /* I'd prefer if this were infinite, but Sun RPC
		      * dereferences the timeval and therefore we
		      * can't send NULL. */
  tv.tv_usec = 0;
  
  if(!clnt_control(clnt, CLSET_TIMEOUT, (char *)&tv)) {
    fprintf(stderr, "rpc_init: changing timeout failed");
    clnt_destroy(clnt);
    return NULL;
  }
  
  return clnt;
}


int
compress_file(char *filename, char *new_filename) {
  int err;
  char command[ARG_MAX];
  char nf[PATH_MAX];

  if(filename == NULL || strlen(filename) <= 0)
    return -1;
  

  snprintf(nf, PATH_MAX, "%s.gz", filename);

  snprintf(command, ARG_MAX, "gzip -c -9 %s > %s", filename, nf);
  err = system(command);
  if(err < 0) {
    perror("system");
    return -1;
  }

  if(new_filename != NULL)
    strcpy(new_filename, nf);

  return 0;
}



int
decompress_file(char *filename, char *new_filename) {
  int err;
  char command[ARG_MAX];

  if(filename == NULL || strlen(filename) <= 0)
    return -1;
  
  snprintf(command, ARG_MAX, "gunzip %s", filename);
  err = system(command);
  if(err < 0) {
    perror("system");
    return -1;
  }

  if(new_filename != NULL) {
    int len;

    strncpy(new_filename, filename, PATH_MAX);
    len = strlen(filename);
    new_filename[len-3] = '\0'; //remove .gz extension
  }


  return 0;
}
