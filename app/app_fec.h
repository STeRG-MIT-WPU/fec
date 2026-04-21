/*
 * app_fec.h — STeRG FEC on-board test harness for NUCLEO-H755ZI-Q.
 *
 * Flow:
 *   MP257 /dev/ttyUSB0 ──► USART2 (MP257_UART)
 *                              │
 *                              ▼
 *                        CCSDS reassembly
 *                              │
 *                              ▼
 *                        RS(255,223) + Conv 1/2 (HK chain)
 *                              │
 *                              ▼
 *                        Self-decode round-trip
 *                              │
 *                              ▼
 *                        USART3 (VCP) ──► host PC terminal
 *
 * Expected CubeMX config:
 *   * USART2  115200 8N1, global IT ON   — data in from MP257
 *   * USART3  115200 8N1                  — VCP / test log out
 *   * Heap   _Min_Heap_Size >= 0x8000     — decoder needs ~25 KB
 *
 * Wiring (NUCLEO-H755ZI-Q):
 *   * USART3 uses the built-in ST-LINK VCP (USB to PC), no wiring.
 *   * USART2 PD5 (TX) / PD6 (RX)  on CN9
 *     - Wire  MP257 UART_TX  →  H755 PD6 (USART2_RX)
 *     - Wire  MP257 GND       →  H755 GND
 *     - Optional back-channel: H755 PD5 → MP257 UART_RX
 */
#ifndef APP_FEC_H
#define APP_FEC_H

#include <stdint.h>
#include <stddef.h>

#include "main.h"        /* brings in HAL + UART_HandleTypeDef */

/* One-shot init.
 *   mp257_uart : HAL handle for the MP257 link (e.g. &huart2)
 *   vcp_uart   : HAL handle for the VCP      (e.g. &huart3)
 * Returns 0 on success, negative on allocation failure. */
int APP_FEC_Init(UART_HandleTypeDef *mp257_uart,
                 UART_HandleTypeDef *vcp_uart);

/* Call repeatedly from main(). Drains any bytes from the ring buffer,
 * reassembles CCSDS packets, and runs FEC round-trip tests. Non-blocking. */
void APP_FEC_Loop(void);

#endif /* APP_FEC_H */
