#ifndef GETSOCKET_H
#define GETSOCKET_H

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

int GetSocket(sockaddr_in &local, socklen_t &len);

#endif
