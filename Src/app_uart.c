/*
 * app_uart.c — interrupt-driven RX ring buffer.
 */
#include "app_uart.h"

#include "main.h"        /* provides HAL types and UART_HandleTypeDef  */

#define APP_UART_RING_LEN 2048   /* power of two keeps masking cheap   */

static UART_HandleTypeDef *s_huart;

static volatile uint8_t s_ring[APP_UART_RING_LEN];
static volatile uint16_t s_head;   /* consumer / read index            */
static volatile uint16_t s_tail;   /* producer / write index           */
static volatile uint8_t  s_rx_byte; /* HAL scratch for single-byte IT  */

static inline uint16_t ring_used(void) {
    return (uint16_t)(s_tail - s_head);
}

void APP_UART_Init(UART_HandleTypeDef *huart) {
    s_huart = huart;
    s_head = 0;
    s_tail = 0;
    HAL_UART_Receive_IT(s_huart, (uint8_t *)&s_rx_byte, 1);
}

void APP_UART_OnRxByte(UART_HandleTypeDef *huart) {
    if (huart != s_huart) return;

    if (ring_used() < APP_UART_RING_LEN) {
        s_ring[s_tail & (APP_UART_RING_LEN - 1)] = s_rx_byte;
        s_tail++;
    }
    /* If full, byte is dropped. Happens only under sustained overflow. */

    /* Re-arm for next byte */
    HAL_UART_Receive_IT(s_huart, (uint8_t *)&s_rx_byte, 1);
}

size_t APP_UART_Available(void) {
    return ring_used();
}

size_t APP_UART_Read(uint8_t *buf, size_t max_len) {
    size_t n = 0;
    while (n < max_len && s_head != s_tail) {
        buf[n++] = s_ring[s_head & (APP_UART_RING_LEN - 1)];
        s_head++;
    }
    return n;
}

int APP_UART_Peek(size_t offset, uint8_t *b) {
    if (ring_used() <= offset) return 0;
    *b = s_ring[(s_head + offset) & (APP_UART_RING_LEN - 1)];
    return 1;
}

void APP_UART_Consume(size_t n) {
    uint16_t used = ring_used();
    if (n > used) n = used;
    s_head += (uint16_t)n;
}
