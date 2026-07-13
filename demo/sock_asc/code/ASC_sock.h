#ifndef ASC_SOCK_H
#define ASC_SOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CANopen.h"
#include <winsock2.h>
#include <ws2tcpip.h>

typedef struct {
    SOCKET sockfd;
    SOCKET clientSockfd;
    struct sockaddr_in addr;
    char host[64];
    int port;
} ASC_socket_t;

typedef struct {
    CO_t* pCO;
    ASC_socket_t* socket;
} ASC_sock_t;

CO_ReturnError_t ASC_sock_init(ASC_sock_t* ascSock, CO_t* CO, ASC_socket_t* socket);
void ASC_sock_process(ASC_sock_t* ascSock, bool_t enable, uint32_t timeDifference_us);
void ASC_sock_close(ASC_sock_t* ascSock);

#ifdef __cplusplus
}
#endif

#endif
