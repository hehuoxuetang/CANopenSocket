#ifndef ASC_UART_H
#define ASC_UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CANopen.h"
#include "wuart.h"

typedef struct {
    CO_t* pCO;
    void* hWuart;
    uint8_t uartId;
    uint32_t baudrate;
} ASC_uart_t;

CO_ReturnError_t ASC_uart_init(ASC_uart_t* ascUart, CO_t* CO, uint8_t uartId, uint32_t baudrate);
void ASC_uart_process(ASC_uart_t* ascUart, bool_t enable, uint32_t timeDifference_us);
void ASC_uart_close(ASC_uart_t* ascUart);

#ifdef __cplusplus
}
#endif

#endif