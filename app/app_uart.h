/*
 * app_uart.h — simple interrupt-driven RX ring buffer for the MP257 link.
 *
 * Usage (CubeIDE, HAL):
 *   1. In CubeMX enable USART2 (PD5/PD6), 115200 8N1, global IT on.
 *   2. In main.c call APP_UART_Init(&huart2) after MX_USART2_UART_Init().
 *   3. In main.c, implement HAL_UART_RxCpltCallback:
 *          void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
 *              APP_UART_OnRxByte(huart);
 *          }
 *   4. Read bytes with APP_UART_Read() from the main loop.
 */
#ifndef APP_UART_H
#define APP_UART_H

#include <stddef.h>
#include <stdint.h>

#ifndef STM32_HAL_INCLUDED
/* Caller is expected to have pulled in the HAL already via main.h. */
#endif

typedef struct UART_HandleTypeDef UART_HandleTypeDef;

/* Start interrupt-driven reception on the given UART. */
void APP_UART_Init(UART_HandleTypeDef *huart);

/* Call from HAL_UART_RxCpltCallback for the MP257-link UART. */
void APP_UART_OnRxByte(UART_HandleTypeDef *huart);

/* Number of bytes waiting in the RX ring. */
size_t APP_UART_Available(void);

/* Copy up to max_len bytes into buf. Returns bytes actually copied. */
size_t APP_UART_Read(uint8_t *buf, size_t max_len);

/* Peek at byte at offset from head without removing it.
 * Returns 1 if byte available (written to *b), 0 otherwise. */
int APP_UART_Peek(size_t offset, uint8_t *b);

/* Discard n bytes from the head. */
void APP_UART_Consume(size_t n);

#endif /* APP_UART_H */
