/*
 * CAN module object for generic microcontroller.
 *
 * This file is a template for other microcontrollers.
 *
 * @file        CO_driver.c
 * @ingroup     CO_driver
 * @author      Janez Paternoster
 * @copyright   2004 - 2020 Janez Paternoster
 *
 * This file is part of <https://github.com/CANopenNode/CANopenNode>, a CANopen Stack.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this
 * file except in compliance with the License. You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and limitations under the License.
 */

#include <stdio.h>
#include "301/CO_driver.h"
#include "CO_driver_target.h"


static int winsockInitialized = 0;

static void initWinsock(void) {
    if (!winsockInitialized) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        winsockInitialized = 1;
    }
}

static int setNonBlocking(SOCKET sockfd) {
    u_long mode = 1;
    return ioctlsocket(sockfd, FIONBIO, &mode);
}

/* Establish socket connection. Called once from main() after argument parsing.
 * Server: bind + listen + blocking accept until client connects.
 * Client: connect to server.
 * Returns 0 on success, negative on error. */
int
CO_CANconnect(CAN_socket_t* canSock) {
    if (canSock == NULL) {
        return -1;
    }

    initWinsock();

    printf("CAN socket: creating TCP socket...\n");

    canSock->sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (canSock->sockfd == INVALID_SOCKET) {
        printf("CAN socket: ERROR - failed to create socket\n");
        return -2;
    }

    memset(&canSock->addr, 0, sizeof(canSock->addr));
    canSock->addr.sin_family = AF_INET;
    canSock->addr.sin_port = htons(canSock->port);
    canSock->addr.sin_addr.s_addr = inet_addr(canSock->host);

    if (canSock->isServer) {
        int optval = 1;
        setsockopt(canSock->sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

        printf("CAN server: binding to %s:%d...\n", canSock->host, canSock->port);
        if (bind(canSock->sockfd, (struct sockaddr*)&canSock->addr, sizeof(canSock->addr)) == SOCKET_ERROR) {
            printf("CAN server: ERROR - bind failed\n");
            closesocket(canSock->sockfd);
            canSock->sockfd = INVALID_SOCKET;
            return -3;
        }

        if (listen(canSock->sockfd, 1) == SOCKET_ERROR) {
            printf("CAN server: ERROR - listen failed\n");
            closesocket(canSock->sockfd);
            canSock->sockfd = INVALID_SOCKET;
            return -4;
        }

        printf("CAN server: waiting for client connection on %s:%d...\n", canSock->host, canSock->port);
        fflush(stdout);

        /* blocking accept - waits for client */
        {
            struct sockaddr_in clientAddr;
            int clientLen = sizeof(clientAddr);
            SOCKET clientSock = accept(canSock->sockfd, (struct sockaddr*)&clientAddr, &clientLen);

            if (clientSock == INVALID_SOCKET) {
                printf("CAN server: ERROR - accept failed\n");
                closesocket(canSock->sockfd);
                canSock->sockfd = INVALID_SOCKET;
                return -5;
            }

            closesocket(canSock->sockfd);
            canSock->sockfd = clientSock;
            printf("CAN server: client connected from %s:%d\n",
                   inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
        }

        setNonBlocking(canSock->sockfd);
    } else {
        printf("CAN client: connecting to %s:%d...\n", canSock->host, canSock->port);
        fflush(stdout);

        if (connect(canSock->sockfd, (struct sockaddr*)&canSock->addr, sizeof(canSock->addr)) == SOCKET_ERROR) {
            printf("CAN client: ERROR - connect to %s:%d failed\n", canSock->host, canSock->port);
            closesocket(canSock->sockfd);
            canSock->sockfd = INVALID_SOCKET;
            return -6;
        }

        setNonBlocking(canSock->sockfd);
        printf("CAN client: connected to %s:%d\n", canSock->host, canSock->port);
    }

    return 0;
}

/* Close socket connection. Called once from main() on program exit. */
void
CO_CANdisconnect(CAN_socket_t* canSock) {
    if (canSock != NULL && canSock->sockfd != INVALID_SOCKET) {
        closesocket(canSock->sockfd);
        canSock->sockfd = INVALID_SOCKET;
    }
}

void
CO_CANsetConfigurationMode(void* CANptr) {
    /* Put CAN module in configuration mode.
     * Socket 保持连接，仅通过 CANnormal 标志停止数据收发。
     */
    (void)CANptr;
}

void
CO_CANsetNormalMode(CO_CANmodule_t* CANmodule) {
    /* Put CAN module in normal mode.
     * Socket 已在 main() 中建立连接，此处仅恢复数据收发。 */
    if (CANmodule != NULL) {
        CANmodule->CANnormal = true;
    }
}

CO_ReturnError_t
CO_CANmodule_init(CO_CANmodule_t* CANmodule, void* CANptr, CO_CANrx_t rxArray[], uint16_t rxSize, CO_CANtx_t txArray[],
                  uint16_t txSize, uint16_t CANbitRate) {
    uint16_t i;
    CAN_socket_t* canSock = (CAN_socket_t*)CANptr;

    (void)CANbitRate;
    (void)canSock;
    /* verify arguments */
    if (CANmodule == NULL || rxArray == NULL || txArray == NULL) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    /* Initialize critical sections */
    InitializeCriticalSection(&CANmodule->lockCanSend);
    InitializeCriticalSection(&CANmodule->lockEmcy);
    InitializeCriticalSection(&CANmodule->lockOd);

    /* Configure object variables */
    CANmodule->CANptr = CANptr;
    CANmodule->rxArray = rxArray;
    CANmodule->rxSize = rxSize;
    CANmodule->txArray = txArray;
    CANmodule->txSize = txSize;
    CANmodule->CANerrorStatus = 0;
    CANmodule->CANnormal = false;
    CANmodule->useCANrxFilters = (rxSize <= 32U) ? true : false; /* microcontroller dependent */
    CANmodule->bufferInhibitFlag = false;
    CANmodule->firstCANtxMessage = true;
    CANmodule->CANtxCount = 0U;
    CANmodule->errOld = 0U;

    for (i = 0U; i < rxSize; i++) {
        rxArray[i].ident = 0U;
        rxArray[i].mask = 0xFFFFU;
        rxArray[i].object = NULL;
        rxArray[i].CANrx_callback = NULL;
    }
    for (i = 0U; i < txSize; i++) {
        txArray[i].bufferFull = false;
    }

    return CO_ERROR_NO;
}

void
CO_CANmodule_disable(CO_CANmodule_t* CANmodule) {
    if (CANmodule != NULL && CANmodule->CANptr != NULL) {
        /* Cleanup critical sections (socket stays connected, managed by main) */
        DeleteCriticalSection(&CANmodule->lockCanSend);
        DeleteCriticalSection(&CANmodule->lockEmcy);
        DeleteCriticalSection(&CANmodule->lockOd);
    }
}

CO_ReturnError_t
CO_CANrxBufferInit(CO_CANmodule_t* CANmodule, uint16_t index, uint16_t ident, uint16_t mask, bool_t rtr, void* object,
                   void (*CANrx_callback)(void* object, void* message)) {
    CO_ReturnError_t ret = CO_ERROR_NO;

    if ((CANmodule != NULL) && (object != NULL) && (CANrx_callback != NULL) && (index < CANmodule->rxSize)) {
        /* buffer, which will be configured */
        CO_CANrx_t* buffer = &CANmodule->rxArray[index];

        /* Configure object variables */
        buffer->object = object;
        buffer->CANrx_callback = CANrx_callback;

        /* CAN identifier and CAN mask, bit aligned with CAN module. Different on different microcontrollers. */
        buffer->ident = ident & 0x07FFU;
        if (rtr) {
            buffer->ident |= 0x0800U;
        }
        buffer->mask = (mask & 0x07FFU) | 0x0800U;

        /* Set CAN hardware module filter and mask. */
        if (CANmodule->useCANrxFilters) {}
    } else {
        ret = CO_ERROR_ILLEGAL_ARGUMENT;
    }

    return ret;
}

CO_CANtx_t*
CO_CANtxBufferInit(CO_CANmodule_t* CANmodule, uint16_t index, uint16_t ident, bool_t rtr, uint8_t noOfBytes,
                   bool_t syncFlag) {
    CO_CANtx_t* buffer = NULL;

    if ((CANmodule != NULL) && (index < CANmodule->txSize)) {
        buffer = &CANmodule->txArray[index];
        buffer->ident = ((uint32_t)ident & 0x07FFU) | ((uint32_t)(rtr ? 0x8000U : 0U));
        buffer->DLC = noOfBytes;
        buffer->bufferFull = false;
        buffer->syncFlag = syncFlag;
    }

    return buffer;
}

CO_ReturnError_t
CO_CANsend(CO_CANmodule_t* CANmodule, CO_CANtx_t* buffer) {
    CO_ReturnError_t err = CO_ERROR_NO;

    /* Verify overflow */
    if (buffer->bufferFull) {
        if (!CANmodule->firstCANtxMessage) {
            /* don't set error, if bootup message is still on buffers */
            CANmodule->CANerrorStatus |= CO_CAN_ERRTX_OVERFLOW;
        }
        err = CO_ERROR_TX_OVERFLOW;
    }

    CO_LOCK_CAN_SEND(CANmodule);
    /* if CAN TX buffer is free, copy message to it */
    CAN_socket_t* canSock = (CAN_socket_t*)CANmodule->CANptr;

    if (CANmodule->CANnormal && canSock != NULL && canSock->sockfd != INVALID_SOCKET && CANmodule->CANtxCount == 0) {
            CAN_frame_t frame;

            frame.DLC = buffer->DLC;
            if (buffer->ident & 0x00008000U) frame.DLC |= 0x80U;
            frame.ident = htonl(buffer->ident);
            memcpy(frame.data, buffer->data, frame.DLC & 0x0FU);

            int n = send(canSock->sockfd, (const char*)&frame, sizeof(CAN_frame_t), 0);

            if (n == sizeof(CAN_frame_t)) {
                printf("[TX] COB-ID=0x%03X DLC=%d\n", buffer->ident & 0x07FFU, frame.DLC & 0x0FU);
            CANmodule->bufferInhibitFlag = buffer->syncFlag;
            if (CANmodule->firstCANtxMessage) {
                CANmodule->firstCANtxMessage = false;
            }
        } else {
            printf("[TX] COB-ID=0x%03X queued (send error)\n", buffer->ident & 0x07FFU);
            buffer->bufferFull = true;
            CANmodule->CANtxCount++;
        }
    } else {
        buffer->bufferFull = true;
        CANmodule->CANtxCount++;
    }

    CO_UNLOCK_CAN_SEND(CANmodule);

    return err;
}

void
CO_CANclearPendingSyncPDOs(CO_CANmodule_t* CANmodule) {
    uint32_t tpdoDeleted = 0U;

    CO_LOCK_CAN_SEND(CANmodule);
    /* Abort message from CAN module, if there is synchronous TPDO.
     * Take special care with this functionality. */
    if (/* messageIsOnCanBuffer && */ CANmodule->bufferInhibitFlag) {
        /* clear TXREQ */
        CANmodule->bufferInhibitFlag = false;
        tpdoDeleted = 1U;
    }
    /* delete also pending synchronous TPDOs in TX buffers */
    if (CANmodule->CANtxCount != 0U) {
        uint16_t i;
        CO_CANtx_t* buffer = &CANmodule->txArray[0];
        for (i = CANmodule->txSize; i > 0U; i--) {
            if (buffer->bufferFull) {
                if (buffer->syncFlag) {
                    buffer->bufferFull = false;
                    CANmodule->CANtxCount--;
                    tpdoDeleted = 2U;
                }
            }
            buffer++;
        }
    }
    CO_UNLOCK_CAN_SEND(CANmodule);

    if (tpdoDeleted != 0U) {
        CANmodule->CANerrorStatus |= CO_CAN_ERRTX_PDO_LATE;
    }
}

/* Get error counters from the module. If necessary, function may use different way to determine errors. */
static uint16_t rxErrors = 0, txErrors = 0, overflow = 0;

void
CO_CANmodule_process(CO_CANmodule_t* CANmodule) {
    uint32_t err;

    err = ((uint32_t)txErrors << 16) | ((uint32_t)rxErrors << 8) | overflow;

    if (CANmodule->errOld != err) {
        uint16_t status = CANmodule->CANerrorStatus;

        CANmodule->errOld = err;

        if (txErrors >= 256U) {
            /* bus off */
            status |= CO_CAN_ERRTX_BUS_OFF;
        } else {
            /* recalculate CANerrorStatus, first clear some flags */
            status &= 0xFFFF
                      ^ (CO_CAN_ERRTX_BUS_OFF | CO_CAN_ERRRX_WARNING | CO_CAN_ERRRX_PASSIVE | CO_CAN_ERRTX_WARNING
                         | CO_CAN_ERRTX_PASSIVE);

            /* rx bus warning or passive */
            if (rxErrors >= 128) {
                status |= CO_CAN_ERRRX_WARNING | CO_CAN_ERRRX_PASSIVE;
            } else if (rxErrors >= 96) {
                status |= CO_CAN_ERRRX_WARNING;
            }

            /* tx bus warning or passive */
            if (txErrors >= 128) {
                status |= CO_CAN_ERRTX_WARNING | CO_CAN_ERRTX_PASSIVE;
            } else if (txErrors >= 96) {
                status |= CO_CAN_ERRTX_WARNING;
            }

            /* if not tx passive clear also overflow */
            if ((status & CO_CAN_ERRTX_PASSIVE) == 0) {
                status &= 0xFFFF ^ CO_CAN_ERRTX_OVERFLOW;
            }
        }

        if (overflow != 0) {
            /* CAN RX bus overflow */
            status |= CO_CAN_ERRRX_OVERFLOW;
        }

        CANmodule->CANerrorStatus = status;
    }
}

void
CO_CANinterrupt(CO_CANmodule_t* CANmodule) {
    /* 适配说明：调用 recv() 接收数据，匹配过滤器后调用回调；处理发送队列中的待发送消息
     */
    CAN_socket_t* canSock = (CAN_socket_t*)CANmodule->CANptr;

    if (canSock == NULL || canSock->sockfd == INVALID_SOCKET || !CANmodule->CANnormal) {
        return;
    }

    CAN_frame_t frame;
    int nbytes = recv(canSock->sockfd, (char*)&frame, sizeof(CAN_frame_t), 0);

    if (nbytes > 0) {
        CO_CANrxMsg_t rcvMsg;

        printf("[RX] COB-ID=0x%03X DLC=%d%s\n", (unsigned int)(ntohl(frame.ident) & 0x07FFU),
               frame.DLC & 0x0FU, (frame.DLC & 0x80U) ? " ext" : "");

        uint16_t index;
        uint32_t rcvMsgIdent;
        CO_CANrx_t* buffer = NULL;
        bool_t msgMatched = false;

        rcvMsg.ident = ntohl(frame.ident) & 0x07FFU;
        if (frame.DLC & 0x80U) {
            rcvMsg.ident |= 0x0800U;  /* extended frame flag from DLC bit 7 */
        }
        rcvMsg.DLC = frame.DLC & 0x0FU;
        memcpy(rcvMsg.data, frame.data, rcvMsg.DLC);

        rcvMsgIdent = rcvMsg.ident;

        if (CANmodule->useCANrxFilters) {
            buffer = &CANmodule->rxArray[0];
            for (index = 0; index < CANmodule->rxSize; index++) {
                if (((rcvMsgIdent ^ buffer->ident) & buffer->mask) == 0U) {
                    msgMatched = true;
                    break;
                }
                buffer++;
            }
        } else {
            buffer = &CANmodule->rxArray[0];
            for (index = CANmodule->rxSize; index > 0U; index--) {
                if (((rcvMsgIdent ^ buffer->ident) & buffer->mask) == 0U) {
                    msgMatched = true;
                    break;
                }
                buffer++;
            }
        }

        if (msgMatched && (buffer != NULL) && (buffer->CANrx_callback != NULL)) {
            buffer->CANrx_callback(buffer->object, (void*)&rcvMsg);
        }
    } else if (nbytes == 0) {
        /* Peer disconnected - socket stays open, CANnormal flag stops further IO */
        printf("[RX] peer disconnected\n");
        CANmodule->CANnormal = false;
    }

    if (CANmodule->CANtxCount > 0U) {
        uint16_t i;
        CO_CANtx_t* buffer = &CANmodule->txArray[0];

        for (i = CANmodule->txSize; i > 0U; i--) {
            if (buffer->bufferFull) {
                CAN_frame_t frame;

                frame.DLC = buffer->DLC;
                if (buffer->ident & 0x00008000U) frame.DLC |= 0x80U;
                frame.ident = htonl(buffer->ident);
                memcpy(frame.data, buffer->data, frame.DLC & 0x0FU);

                int n = send(canSock->sockfd, (const char*)&frame, sizeof(CAN_frame_t), 0);

                if (n == sizeof(CAN_frame_t)) {
                    printf("[TX] COB-ID=0x%03X DLC=%d (from queue)\n", buffer->ident & 0x07FFU, frame.DLC & 0x0FU);
                    buffer->bufferFull = false;
                    CANmodule->CANtxCount--;
                    CANmodule->bufferInhibitFlag = buffer->syncFlag;

                    if (CANmodule->firstCANtxMessage) {
                        CANmodule->firstCANtxMessage = false;
                    }
                }

                break;
            }
            buffer++;
        }

        if (i == 0U) {
            CANmodule->CANtxCount = 0U;
        }
    }
}
