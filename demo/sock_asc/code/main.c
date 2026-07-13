/*
 * CANopen main program file.
 *
 * This file is a template for other microcontrollers.
 *
 * @file        main_generic.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CANopen.h"
#include "OD.h"
#include "CO_storage_sock.h"
#include "CO_driver_target.h"
#include "ASC_uart.h"

#define log_printf(macropar_message, ...) printf(macropar_message, ##__VA_ARGS__)

/* default values for CO_CANopenInit() */
#define NMT_CONTROL                                                                                                    \
    CO_NMT_STARTUP_TO_OPERATIONAL                                                                                      \
    | CO_NMT_ERR_ON_ERR_REG | CO_ERR_REG_GENERIC_ERR | CO_ERR_REG_COMMUNICATION
#define FIRST_HB_TIME        500
#define SDO_SRV_TIMEOUT_TIME 1000
#define SDO_CLI_TIMEOUT_TIME 500
#define SDO_CLI_BLOCK        false
#define OD_STATUS_BITS       NULL

/* Global variables and objects */
CO_t* CO = NULL; /* CANopen object */
uint8_t LED_red, LED_green;
ASC_uart_t ascUart;

const CAN_socket_t canSocket_def = {
    .sockfd = INVALID_SOCKET,
    .isServer = DEFAULT_SERVER,
    .host = DEFAULT_HOST,
    .port = DEFAULT_PORT
};

/* Thread control */
static volatile CAN_thread_t canThread = {
    .intThrdRun = 0,
    .tmrThrdRun = 0,
    .intFuncRun = 0,
    .tmrFuncRun = 0
};

extern void CO_CANinterrupt(CO_CANmodule_t* CANmodule);
extern void tmrTask_thread(void);

/* CAN interrupt thread - continuously processes incoming/outgoing CAN messages */
static DWORD WINAPI
canInterruptThreadFunc(LPVOID lpParam) {
    (void)lpParam;
    while (canThread.intThrdRun) {
		if(canThread.intFuncRun){
			if (CO != NULL && CO->CANmodule != NULL) {
	            CO_CANinterrupt(CO->CANmodule);
	        }
		}        
        Sleep(1);
    }
    return 0;
}

/* Timer thread - executes PDO/SYNC processing at 1 millisecond intervals */
static DWORD WINAPI
tmrThreadFunc(LPVOID lpParam) {
    (void)lpParam;
    while (canThread.tmrThrdRun) {
		if(canThread.tmrFuncRun){
			tmrTask_thread();
		}
        Sleep(1);
    }
    return 0;
}

/* main ***********************************************************************/
int
main(int argc, char* argv[]) {
    CO_ReturnError_t err;
    CO_NMT_reset_cmd_t reset = CO_RESET_NOT;
    uint32_t heapMemoryUsed;
    CAN_socket_t canSocket;         /* CAN module address */
    uint8_t pendingNodeId = 10;     /* read from dip switches or nonvolatile memory, configurable by LSS slave */
    uint8_t activeNodeId = 10;      /* Copied from CO_pendingNodeId in the communication reset section */
    uint16_t pendingBitRate = 125;  /* read from dip switches or nonvolatile memory, configurable by LSS slave */

	canSocket = canSocket_def;
    /* Parse command line arguments */
    if (argc >= 2) {
        /* arg1: ip:port */
        char* colon = strchr(argv[1], ':');
        if (colon != NULL) {
            *colon = '\0';
            strncpy(canSocket.host, argv[1], sizeof(canSocket.host) - 1);
            canSocket.host[sizeof(canSocket.host) - 1] = '\0';
            canSocket.port = atoi(colon + 1);
            if (canSocket.port <= 0) {
                canSocket.port = DEFAULT_PORT;
            }
            *colon = ':';
        } else {
            /* only IP, no port */
            strncpy(canSocket.host, argv[1], sizeof(canSocket.host) - 1);
            canSocket.host[sizeof(canSocket.host) - 1] = '\0';
        }
    }
    uint8_t uartId = 1;
    if (argc >= 3) {
        /* arg2: isServer (0=client, 1=server) */
        canSocket.isServer = (atoi(argv[2]) != 0) ? 1 : 0;
    }
    if (argc >= 4) {
        /* arg3: COM port number */
        uartId = (uint8_t)atoi(argv[3]);
        if (uartId == 0) {
            uartId = 1;
        }
    }

    log_printf("Config: host=%s, port=%d, mode=%s, COM=%d\n",
               canSocket.host, canSocket.port,
               canSocket.isServer ? "Server" : "Client", uartId);

    /* Establish socket connection */
    {
        int connResult = CO_CANconnect(&canSocket);
        if (connResult != 0) {
            log_printf("Error: socket connection failed: %d\n", connResult);
            return 1;
        }
        log_printf("Socket connected: %s:%d\n", canSocket.host, canSocket.port);
    }
	// create interrupt and timer thread
	canThread.tmrThrdRun = 1;
	CreateThread(NULL, 0, tmrThreadFunc, NULL, 0, NULL);
	canThread.intThrdRun = 1;
    CreateThread(NULL, 0, canInterruptThreadFunc, NULL, 0, NULL);

#if (CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE
    CO_storage_t storage;
    CO_storage_entry_t storageEntries[] = {{.addr = &OD_PERSIST_COMM,
                                            .len = sizeof(OD_PERSIST_COMM),
                                            .subIndexOD = 2,
                                            .attr = CO_storage_cmd | CO_storage_restore,
                                            .addrNV = STORE_FILE}};
    uint8_t storageEntriesCount = sizeof(storageEntries) / sizeof(storageEntries[0]);
    uint32_t storageInitError = 0;
#endif

    /* Configure microcontroller. */

    /* Allocate memory */
    CO_config_t* config_ptr = NULL;
#ifdef CO_MULTIPLE_OD
    /* example usage of CO_MULTIPLE_OD (but still single OD here) */
    CO_config_t co_config = {0};
    OD_INIT_CONFIG(co_config); /* helper macro from OD.h */
    co_config.CNT_LEDS = 1;
    co_config.CNT_LSS_SLV = 1;
    config_ptr = &co_config;
#endif /* CO_MULTIPLE_OD */
    CO = CO_new(config_ptr, &heapMemoryUsed);
    if (CO == NULL) {
        log_printf("Error: Can't allocate memory\n");
        return 0;
    } else {
        log_printf("Allocated %u bytes for CANopen objects\n", heapMemoryUsed);
    }

#if (CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE
    err = CO_storageBlank_init(&storage, CO->CANmodule, OD_ENTRY_H1010_storeParameters,
                               OD_ENTRY_H1011_restoreDefaultParameters, storageEntries, storageEntriesCount,
                               &storageInitError);

    if (err != CO_ERROR_NO && err != CO_ERROR_DATA_CORRUPT) {
        log_printf("Error: Storage %d\n", storageInitError);
        return 0;
    }
#endif

    while (reset != CO_RESET_APP) {
        /* CANopen communication reset - initialize CANopen objects *******************/
        log_printf("CANopenNode - Reset communication...\n");

        /* Wait rt_thread. */
        CO->CANmodule->CANnormal = false;

        /* Enter CAN configuration. */
        CO_CANsetConfigurationMode((void*)&canSocket);
        CO_CANmodule_disable(CO->CANmodule);

        /* initialize CANopen */
        err = CO_CANinit(CO, &canSocket, pendingBitRate);
        if (err != CO_ERROR_NO) {
            log_printf("Error: CAN initialization failed: %d\n", err);
            return 0;
        }

        CO_LSS_address_t lssAddress = {.identity = {.vendorID = OD_PERSIST_COMM.x1018_identity.vendor_ID,
                                                    .productCode = OD_PERSIST_COMM.x1018_identity.productCode,
                                                    .revisionNumber = OD_PERSIST_COMM.x1018_identity.revisionNumber,
                                                    .serialNumber = OD_PERSIST_COMM.x1018_identity.serialNumber}};
        err = CO_LSSinit(CO, &lssAddress, &pendingNodeId, &pendingBitRate);
        if (err != CO_ERROR_NO) {
            log_printf("Error: LSS slave initialization failed: %d\n", err);
            return 0;
        }

        activeNodeId = pendingNodeId;
        uint32_t errInfo = 0;

        err = CO_CANopenInit(CO,                   /* CANopen object */
                             NULL,                 /* alternate NMT */
                             NULL,                 /* alternate em */
                             OD,                   /* Object dictionary */
                             OD_STATUS_BITS,       /* Optional OD_statusBits */
                             NMT_CONTROL,          /* CO_NMT_control_t */
                             FIRST_HB_TIME,        /* firstHBTime_ms */
                             SDO_SRV_TIMEOUT_TIME, /* SDOserverTimeoutTime_ms */
                             SDO_CLI_TIMEOUT_TIME, /* SDOclientTimeoutTime_ms */
                             SDO_CLI_BLOCK,        /* SDOclientBlockTransfer */
                             activeNodeId, &errInfo);
        if (err != CO_ERROR_NO && err != CO_ERROR_NODE_ID_UNCONFIGURED_LSS) {
            if (err == CO_ERROR_OD_PARAMETERS) {
                log_printf("Error: Object Dictionary entry 0x%X\n", errInfo);
            } else {
                log_printf("Error: CANopen initialization failed: %d\n", err);
            }
            return 0;
        }

        err = CO_CANopenInitPDO(CO, CO->em, OD, activeNodeId, &errInfo);
        if (err != CO_ERROR_NO && err != CO_ERROR_NODE_ID_UNCONFIGURED_LSS) {
            if (err == CO_ERROR_OD_PARAMETERS) {
                log_printf("Error: Object Dictionary entry 0x%X\n", errInfo);
            } else {
                log_printf("Error: PDO initialization failed: %d\n", err);
            }
            return 0;
        }

        /* Configure Timer interrupt function for execution every 1 millisecond */
		canThread.tmrFuncRun = 1;
		
        /* Configure CAN transmit and receive interrupt */
		canThread.intFuncRun = 1;
		
        /* Configure CANopen callbacks, etc */
        if (!CO->nodeIdUnconfigured) {

#if (CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE
            if (storageInitError != 0) {
                CO_errorReport(CO->em, CO_EM_NON_VOLATILE_MEMORY, CO_EMC_HARDWARE, storageInitError);
            }
#endif

            /* Initialize ASC_uart gateway */
            err = ASC_uart_init(&ascUart, CO, uartId, 115200);
            if (err != CO_ERROR_NO) {
                log_printf("Warning: ASC_uart initialization failed: %d\n", err);
            } else {
                log_printf("ASC_uart initialized: COM%d, 115200 baud\n", uartId);
            }
        } else {
            log_printf("CANopenNode - Node-id not initialized\n");
        }

        /* start CAN */
        CO_CANsetNormalMode(CO->CANmodule);

        reset = CO_RESET_NOT;

        log_printf("CANopenNode - Running...\n");
        log_printf("Node ID: %d, Bitrate: %dkbps\n", activeNodeId, pendingBitRate);
        fflush(stdout);

        while (reset == CO_RESET_NOT) {
            /* loop for normal program execution ******************************************/

            /* get time difference since last function call */
            uint32_t timeDifference_us = 1000;

            /* CANopen process */
            reset = CO_process(CO, true, timeDifference_us, NULL);
            LED_red = CO_LED_RED(CO->LEDs, CO_LED_CANopen);
            LED_green = CO_LED_GREEN(CO->LEDs, CO_LED_CANopen);

            /* Nonblocking application code may go here. */

            /* Process automatic storage */
		#if (CO_CONFIG_STORAGE) & CO_CONFIG_STORAGE_ENABLE
            CO_storageBlank_auto_process(&storage, false);
		#endif

            /* Process ASC_uart gateway */
            ASC_uart_process(&ascUart, true, timeDifference_us);

            /* optional sleep for short time */
            Sleep(1);
        }

        /* Stop threads before communication reset */
        canThread.tmrFuncRun = 0;
        canThread.intFuncRun = 0;
        Sleep(5);
    }

    /* program exit ***************************************************************/
    /* stop threads */
	canThread.tmrThrdRun = 0;
    canThread.intThrdRun = 0;

    /* delete objects from memory */
    CO_CANsetConfigurationMode((void*)&canSocket);
    CO_delete(CO);

    /* close socket connection */
    CO_CANdisconnect(&canSocket);
    
    /* close ASC_uart */
    ASC_uart_close(&ascUart);

    log_printf("CANopenNode finished\n");

    /* reset */
    return 0;
}

/* timer thread executes in constant intervals ********************************/
void
tmrTask_thread(void) {

//    for (;;) {
        CO_LOCK_OD(CO->CANmodule);
        if (!CO->nodeIdUnconfigured && CO->CANmodule->CANnormal) {
            bool_t syncWas = false;
            /* get time difference since last function call */
            uint32_t timeDifference_us = 1000;

#if (CO_CONFIG_SYNC) & CO_CONFIG_SYNC_ENABLE
            syncWas = CO_process_SYNC(CO, timeDifference_us, NULL);
#endif
#if (CO_CONFIG_PDO) & CO_CONFIG_RPDO_ENABLE
            CO_process_RPDO(CO, syncWas, timeDifference_us, NULL);
#endif
#if (CO_CONFIG_PDO) & CO_CONFIG_TPDO_ENABLE
            CO_process_TPDO(CO, syncWas, timeDifference_us, NULL);
#endif

            /* Further I/O or nonblocking application code may go here. */
        }
        CO_UNLOCK_OD(CO->CANmodule);
//    }
}

/* CAN interrupt function executes on received CAN message ********************/
void /* interrupt */
CO_CAN1InterruptHandler(void) {

    /* clear interrupt flag */
}
