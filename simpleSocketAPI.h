#ifndef _SIMPLESOCKETAPI_H
#define _SIMPLESOCKETAPI_H

#include  <sys/types.h>
#include <sys/socket.h>

int connect2Server(const char *serverName, int port, int *descSock);
int gererSocket(int mode, socklen_t* len, int port);

#endif