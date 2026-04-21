/*
 * app_printf.c — retarget newlib printf() to the VCP UART (ST-LINK/USB).
 *
 * Call APP_Printf_Init(&huart3) once in main(); thereafter printf and
 * puts go out over USART3 (the default ST-LINK VCP on NUCLEO-H755ZI-Q).
 *
 * Uses HAL_UART_Transmit in blocking mode — fine for logging at 115200.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/unistd.h>
#include <errno.h>

#include "main.h"

static UART_HandleTypeDef *s_vcp;

void APP_Printf_Init(UART_HandleTypeDef *huart) {
    s_vcp = huart;
}

/* newlib calls _write through __io_putchar(). Override either; we do _write
 * because it also covers fwrite/puts and is slightly faster. */
int _write(int fd, const char *buf, int len) {
    (void)fd;
    if (!s_vcp || len <= 0) return len;
    HAL_UART_Transmit(s_vcp, (uint8_t *)buf, (uint16_t)len, HAL_MAX_DELAY);
    return len;
}
