/*
 * Device and application specific definitions for CANopenNode.
 *
 * @file        CO_driver_target.h
 * @author      Janez Paternoster
 * @copyright   2021 Janez Paternoster
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

#ifndef CO_DRIVER_TARGET_H
#define CO_DRIVER_TARGET_H

/* This file contains device and application specific definitions. It is included from CO_driver.h, which contains
 * documentation for common definitions below. */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#ifdef CO_DRIVER_CUSTOM
#include "CO_driver_custom.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Stack configuration override default values. For more information see file CO_config.h. */

/* CiA 309-3 Gateway ASCII configuration */
#define CO_CONFIG_GTW                                                                                                  \
    (CO_CONFIG_GTW_ASCII | CO_CONFIG_GTW_ASCII_SDO | CO_CONFIG_GTW_ASCII_NMT | CO_CONFIG_GTW_ASCII_LSS                 \
     | CO_CONFIG_GTW_ASCII_LOG | CO_CONFIG_GTW_ASCII_ERROR_DESC | CO_CONFIG_GTW_ASCII_PRINT_HELP                      \
     | CO_CONFIG_GTW_ASCII_PRINT_LEDS)

/* FIFO configuration for gateway */
#define CO_CONFIG_FIFO                                                                                                 \
    (CO_CONFIG_FIFO_ENABLE | CO_CONFIG_FIFO_ASCII_COMMANDS | CO_CONFIG_FIFO_ASCII_DATATYPES)

/* SDO client configuration for gateway */
#define CO_CONFIG_SDO_CLI                                                                                              \
    (CO_CONFIG_SDO_CLI_ENABLE | CO_CONFIG_SDO_CLI_SEGMENTED)

/* LSS master configuration for gateway */
#define CO_CONFIG_LSS (CO_CONFIG_LSS_SLAVE | CO_CONFIG_LSS_MASTER)

/* NMT master configuration for gateway */
#define CO_CONFIG_NMT (CO_CONFIG_GLOBAL_FLAG_CALLBACK_PRE | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT | CO_CONFIG_NMT_MASTER)

/* Gateway ASCII buffer sizes */
#define CO_CONFIG_GTWA_COMM_BUF_SIZE 256
#define CO_CONFIG_GTWA_LOG_BUF_SIZE 512

/* SDO block download loop count */
#define CO_CONFIG_GTW_BLOCK_DL_LOOP 1


/* Basic definitions. If big endian, CO_SWAP_xx macros must swap bytes. */
#define CO_LITTLE_ENDIAN
#define CO_SWAP_16(x) x
#define CO_SWAP_32(x) x
#define CO_SWAP_64(x) x
/* NULL is defined in stddef.h */
/* true and false are defined in stdbool.h */
/* int8_t to uint64_t are defined in stdint.h */
typedef uint_fast8_t bool_t;
typedef float float32_t;
typedef double float64_t;

#pragma pack(push, 1)
typedef struct {
	uint8_t DLC;
	uint32_t ident;
	uint8_t data[8];
} CO_CANrxMsg_t;
#pragma pack(pop)

/* Access to received CAN message */
#define CO_CANrxMsg_readIdent(msg) (((CO_CANrxMsg_t*)(msg))->ident & 0x07FFU)
#define CO_CANrxMsg_readDLC(msg)   (((CO_CANrxMsg_t*)(msg))->DLC)
#define CO_CANrxMsg_readData(msg)  (((CO_CANrxMsg_t*)(msg))->data)

/* Received message object */
typedef struct {
    uint16_t ident;
    uint16_t mask;
    void* object;
    void (*CANrx_callback)(void* object, void* message);
} CO_CANrx_t;

/* Transmit message object */
typedef struct {
    uint32_t ident;
    uint8_t DLC;
    uint8_t data[8];
    volatile bool_t bufferFull;
    volatile bool_t syncFlag;
} CO_CANtx_t;

/* CAN module object */
typedef struct {
    void* CANptr;
    CO_CANrx_t* rxArray;
    uint16_t rxSize;
    CO_CANtx_t* txArray;
    uint16_t txSize;
    uint16_t CANerrorStatus;
    volatile bool_t CANnormal;
    volatile bool_t useCANrxFilters;
    volatile bool_t bufferInhibitFlag;
    volatile bool_t firstCANtxMessage;
    volatile uint16_t CANtxCount;
    uint32_t errOld;
    CRITICAL_SECTION lockCanSend;
    CRITICAL_SECTION lockEmcy;
    CRITICAL_SECTION lockOd;
} CO_CANmodule_t;

/* Data storage object for one entry */
typedef struct {
    void* addr;
    size_t len;
    uint8_t subIndexOD;
    uint8_t attr;
    /* Additional variables (target specific) */
    void* addrNV;
} CO_storage_entry_t;

/* (un)lock critical section in CO_CANsend() */
#define CO_LOCK_CAN_SEND(CAN_MODULE)   EnterCriticalSection(&(CAN_MODULE)->lockCanSend)
#define CO_UNLOCK_CAN_SEND(CAN_MODULE) LeaveCriticalSection(&(CAN_MODULE)->lockCanSend)

/* (un)lock critical section in CO_errorReport() or CO_errorReset() */
#define CO_LOCK_EMCY(CAN_MODULE)       EnterCriticalSection(&(CAN_MODULE)->lockEmcy)
#define CO_UNLOCK_EMCY(CAN_MODULE)     LeaveCriticalSection(&(CAN_MODULE)->lockEmcy)

/* (un)lock critical section when accessing Object Dictionary */
#define CO_LOCK_OD(CAN_MODULE)         EnterCriticalSection(&(CAN_MODULE)->lockOd)
#define CO_UNLOCK_OD(CAN_MODULE)       LeaveCriticalSection(&(CAN_MODULE)->lockOd)

/* Synchronization between CAN receive and message processing threads. */
#define CO_MemoryBarrier()
#define CO_FLAG_READ(rxNew) ((rxNew) != NULL)
#define CO_FLAG_SET(rxNew)                                                                                             \
    {                                                                                                                  \
        CO_MemoryBarrier();                                                                                            \
        rxNew = (void*)1L;                                                                                             \
    }
#define CO_FLAG_CLEAR(rxNew)                                                                                           \
    {                                                                                                                  \
        CO_MemoryBarrier();                                                                                            \
        rxNew = NULL;                                                                                                  \
    }

// -----------------------------------add by ccq-------------------------------------------//

/* Socket CAN configuration defaults */
#define DEFAULT_SERVER  1  /* 1 for server mode */
#define DEFAULT_PORT    5000
#define DEFAULT_HOST    "0.0.0.0"

#define STORE_FILE		"canStore.bin"
	
typedef struct {
	SOCKET sockfd;
	struct sockaddr_in addr;
	int isServer;
	char host[64];
	int port;
} CAN_socket_t;

typedef CO_CANrxMsg_t CAN_frame_t;

typedef struct {
	int intThrdRun;
	int tmrThrdRun;
	// 仅是模拟can和tmr中断的配置
	int intFuncRun;
	int tmrFuncRun;
} CAN_thread_t;

/* Socket connection management (called from main) */
int  CO_CANconnect(CAN_socket_t* canSock);
void CO_CANdisconnect(CAN_socket_t* canSock);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CO_DRIVER_TARGET_H */
