#include "ASC_sock.h"
#include <string.h>

static bool_t cmdWritten = false;
static size_t ASC_uart_readCallback(void* object, const char* buf, size_t count, uint8_t* connectionOK);

static int ASC_uart_rxEvent(void* pPara, unsigned char* szData, int vLen) {
    CO_t* CO = (CO_t*)pPara;
    if (CO != NULL && szData != NULL && vLen > 0 && CO->gtwa != NULL) {
        uint8_t buf[256];
        int j = 0;
        for (int i = 0; i < vLen && j < sizeof(buf); i++) {
            if (szData[i] != '\r') {
                buf[j++] = szData[i];
            }
        }
        CO_GTWA_write(CO->gtwa, (const char*)buf, (size_t)j);
        cmdWritten = true;
    }
    return vLen;
}

CO_ReturnError_t ASC_uart_init(ASC_uart_t* ascUart, CO_t* CO, uint8_t uartId, uint32_t baudrate) {
    CO_ReturnError_t err;

    if (ascUart == NULL || CO == NULL) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    memset(ascUart, 0, sizeof(ASC_uart_t));
    ascUart->pCO = CO;
    ascUart->uartId = uartId;
    ascUart->baudrate = baudrate;

    ascUart->hWuart = wuart_open((int)uartId, (int)baudrate, 1024);
    if (ascUart->hWuart == NULL) {
        return CO_ERROR_SYSCALL;
    }

    wuart_reg_rx_event(ascUart->hWuart, ASC_uart_rxEvent, CO);

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
            wuart_close(ascUart->hWuart);
            ascUart->hWuart = NULL;
            return err;
        }

        CO_GTWA_initRead(CO->gtwa, ASC_uart_readCallback, ascUart);
    }

    return CO_ERROR_NO;
}

static size_t ASC_uart_readCallback(void* object, const char* buf, size_t count, uint8_t* connectionOK) {
    ASC_uart_t* ascUart = (ASC_uart_t*)object;

    if (ascUart == NULL || ascUart->hWuart == NULL || buf == NULL || count == 0) {
        if (connectionOK != NULL) {
            *connectionOK = 0;
        }
        return 0;
    }

    int n = wuart_write(ascUart->hWuart, buf, (unsigned int)count);

    if (connectionOK != NULL) {
        *connectionOK = (n > 0) ? 1 : 0;
    }

    return (size_t)n;
}

void ASC_uart_process(ASC_uart_t* ascUart, bool_t enable, uint32_t timeDifference_us) {
    if (ascUart != NULL && ascUart->pCO != NULL && ascUart->pCO->gtwa != NULL) {
        CO_GTWA_process(ascUart->pCO->gtwa, enable, timeDifference_us, NULL);
    }
}

void ASC_uart_close(ASC_uart_t* ascUart) {
    if (ascUart == NULL) {
        return;
    }

    if (ascUart->hWuart != NULL) {
        wuart_close(ascUart->hWuart);
        ascUart->hWuart = NULL;
    }
}
