#include "ASC_sock.h"
#include <string.h>
#include <stdio.h>

static bool_t cmdWritten = false;
static size_t ASC_sock_readCallback(void* object, const char* buf, size_t count, uint8_t* connectionOK);

static int setNonBlocking(SOCKET sockfd) {
    u_long mode = 1;
    return ioctlsocket(sockfd, FIONBIO, &mode);
}

CO_ReturnError_t ASC_sock_init(ASC_sock_t* ascSock, CO_t* CO, ASC_socket_t* ascSocket) {
    CO_ReturnError_t err;

    if (ascSock == NULL || CO == NULL || ascSocket == NULL) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    memset(ascSock, 0, sizeof(ASC_sock_t));
    ascSock->pCO = CO;
    ascSock->socket = ascSocket;

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    ascSocket->sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ascSocket->sockfd == INVALID_SOCKET) {
        printf("ASC socket: ERROR - failed to create socket\n");
        return CO_ERROR_SYSCALL;
    }

    int optval = 1;
    setsockopt(ascSocket->sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

    memset(&ascSocket->addr, 0, sizeof(ascSocket->addr));
    ascSocket->addr.sin_family = AF_INET;
    ascSocket->addr.sin_port = htons(ascSocket->port);
    ascSocket->addr.sin_addr.s_addr = inet_addr(ascSocket->host);

    printf("ASC server: binding to %s:%d...\n", ascSocket->host, ascSocket->port);
    if (bind(ascSocket->sockfd, (struct sockaddr*)&ascSocket->addr, sizeof(ascSocket->addr)) == SOCKET_ERROR) {
        printf("ASC server: ERROR - bind failed\n");
        closesocket(ascSocket->sockfd);
        ascSocket->sockfd = INVALID_SOCKET;
        return CO_ERROR_SYSCALL;
    }

    if (listen(ascSocket->sockfd, 1) == SOCKET_ERROR) {
        printf("ASC server: ERROR - listen failed\n");
        closesocket(ascSocket->sockfd);
        ascSocket->sockfd = INVALID_SOCKET;
        return CO_ERROR_SYSCALL;
    }

    setNonBlocking(ascSocket->sockfd);

    printf("ASC server: listening on %s:%d...\n", ascSocket->host, ascSocket->port);

    if (CO->gtwa != NULL) {
        err = CO_GTWA_init(CO->gtwa,
#if (((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII_SDO) != 0)
                           CO->SDOclient, 500, false,
#endif
#if (((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII_NMT) != 0)
                           CO->NMT,
#endif
#if (((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII_LSS) != 0)
                           CO->LSSmaster,
#endif
#if (((CO_CONFIG_GTW)&CO_CONFIG_GTW_ASCII_PRINT_LEDS) != 0)
                           CO->LEDs,
#endif
                           0);

        if (err != CO_ERROR_NO) {
            closesocket(ascSocket->sockfd);
            ascSocket->sockfd = INVALID_SOCKET;
            return err;
        }

        CO_GTWA_initRead(CO->gtwa, ASC_sock_readCallback, ascSock);
    }

    return CO_ERROR_NO;
}

static size_t ASC_sock_readCallback(void* object, const char* buf, size_t count, uint8_t* connectionOK) {
    ASC_sock_t* ascSock = (ASC_sock_t*)object;

    if (ascSock == NULL || ascSock->socket == NULL || buf == NULL || count == 0) {
        if (connectionOK != NULL) {
            *connectionOK = 0;
        }
        return 0;
    }

    if (ascSock->socket->clientSockfd == INVALID_SOCKET) {
        if (connectionOK != NULL) {
            *connectionOK = 0;
        }
        return 0;
    }

    int n = send(ascSock->socket->clientSockfd, buf, (int)count, 0);

    if (connectionOK != NULL) {
        *connectionOK = (n > 0) ? 1 : 0;
    }

    if (n == SOCKET_ERROR) {
        closesocket(ascSock->socket->clientSockfd);
        ascSock->socket->clientSockfd = INVALID_SOCKET;
        printf("ASC server: client disconnected (send error)\n");
        return 0;
    }

    return (size_t)n;
}

void ASC_sock_process(ASC_sock_t* ascSock, bool_t enable, uint32_t timeDifference_us) {
    if (ascSock == NULL || ascSock->pCO == NULL) {
        return;
    }

    ASC_socket_t* socket = ascSock->socket;
    if (socket == NULL || socket->sockfd == INVALID_SOCKET) {
        return;
    }

    if (socket->clientSockfd == INVALID_SOCKET) {
        struct sockaddr_in clientAddr;
        int clientLen = sizeof(clientAddr);
        SOCKET clientSock = accept(socket->sockfd, (struct sockaddr*)&clientAddr, &clientLen);

        if (clientSock != INVALID_SOCKET) {
            socket->clientSockfd = clientSock;
            setNonBlocking(socket->clientSockfd);
            printf("ASC server: client connected from %s:%d\n",
                   inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
        }
    }

    if (socket->clientSockfd != INVALID_SOCKET && ascSock->pCO->gtwa != NULL) {
        char buf[256];
        int n = recv(socket->clientSockfd, buf, sizeof(buf) - 1, 0);

        if (n > 0) {
            buf[n] = '\0';
            int j = 0;
            for (int i = 0; i < n && j < sizeof(buf) - 1; i++) {
                if (buf[i] != '\r') {
                    buf[j++] = buf[i];
                }
            }
            buf[j] = '\0';
            CO_GTWA_write(ascSock->pCO->gtwa, buf, (size_t)j);
            cmdWritten = true;
        } else if (n == 0) {
            closesocket(socket->clientSockfd);
            socket->clientSockfd = INVALID_SOCKET;
            printf("ASC server: client disconnected\n");
        }

        CO_GTWA_process(ascSock->pCO->gtwa, enable, timeDifference_us, NULL);
    }
}

void ASC_sock_close(ASC_sock_t* ascSock) {
    if (ascSock == NULL || ascSock->socket == NULL) {
        return;
    }

    ASC_socket_t* socket = ascSock->socket;

    if (socket->clientSockfd != INVALID_SOCKET) {
        closesocket(socket->clientSockfd);
        socket->clientSockfd = INVALID_SOCKET;
    }

    if (socket->sockfd != INVALID_SOCKET) {
        closesocket(socket->sockfd);
        socket->sockfd = INVALID_SOCKET;
    }
}
