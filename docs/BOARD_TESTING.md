# On-board testing — NUCLEO-H755ZI-Q

End-to-end test of `fec_sterg` using real CCSDS telemetry from the MP257:

```
MP257  /dev/ttyUSB0  ───►  H755  USART2 RX  (data)
                                   │
                                   ▼
                            CCSDS reassembly
                                   │
                                   ▼
                        RS(255,223) + Conv 1/2
                                   │
                                   ▼
                        Self-decode round-trip
                                   │
                                   ▼
                          printf ─► BSP_COM (USART3)
                                   │
                            (ST-LINK USB VCP)
                                   │
                                   ▼
                               Host PC terminal
```

## 1. Wiring

MP257 (raw CCSDS at 115200 8N1 on `/dev/ttyUSB0`) → H755-ZI-Q:

| MP257 | H755 pin | Connector | Role |
|---|---|---|---|
| UART TX  | **PD6** (USART2_RX) | CN9-4 | data in |
| GND      | **GND**             | any GND | reference |
| _(opt.)_ UART RX | PD5 (USART2_TX) | CN9-6 | back-channel |

USART3 (ST-LINK VCP) is wired internally — just plug the ST-LINK USB
lead into your PC. VCP appears as `COMx` on Windows or
`/dev/ttyACM0` on Linux.

## 2. Create the CubeIDE project

1. **File → New → STM32 Project**, Board Selector = **NUCLEO-H755ZI-Q**,
   Targeted Cortex = **M7** → **Finish**. Accept "Initialize peripherals
   to default? Yes".
2. The `.ioc` (CubeMX) file opens automatically.

## 3. Configure peripherals

**USART2** (MP257 link):
- Connectivity → USART2 → Mode = **Asynchronous**
- Confirm **PD5 = USART2_TX**, **PD6 = USART2_RX**
- Parameter Settings: **115200 Bd, 8N1**
- **NVIC Settings → USART2 global interrupt: ON** (mandatory for RX)

**USART3 (VCP):** leave under BSP control — the board preset already
wires it to the ST-LINK USB VCP via `BSP_COM_Init`.

**Ctrl+S** to regenerate.

## 4. Bump heap + stack

Edit `CM7/STM32H755ZITX_FLASH.ld`:

```
_Min_Heap_Size  = 0x8000;     /* 32 KB — decoder needs ~25 KB */
_Min_Stack_Size = 0x1000;
```

## 5. Drop `fec_sterg` files in

Copy the contents of the library folders — **not the folders themselves**
— into the CubeIDE project's Core folders:

```
fec_sterg/Inc/*.h   →   <project>/CM7/Core/Inc/
fec_sterg/Src/*.c   →   <project>/CM7/Core/Src/
```

Windows: open both folder pairs in Explorer, Ctrl+A in the source, drag
into the destination.

CubeIDE auto-picks up everything under `Core/Inc` and `Core/Src` — no
Paths-and-Symbols configuration needed.

## 6. Wire the app into `main.c`

Open **`CM7/Core/Src/main.c`** and add to the USER CODE regions:

```c
/* USER CODE BEGIN Includes */
#include "app_fec.h"
#include "app_uart.h"
/* USER CODE END Includes */
```

```c
/* USER CODE BEGIN 2 */
/* BSP_COM first so printf has somewhere to go */
BspCOMInit.BaudRate   = 115200;
BspCOMInit.WordLength = COM_WORDLENGTH_8B;
BspCOMInit.StopBits   = COM_STOPBITS_1;
BspCOMInit.Parity     = COM_PARITY_NONE;
BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE) {
    Error_Handler();
}

if (APP_FEC_Init(&huart2) != 0) {
    Error_Handler();
}
/* USER CODE END 2 */

/* ... */

while (1)
{
    /* USER CODE BEGIN WHILE */
    APP_FEC_Loop();
    /* USER CODE END WHILE */
}
```

Add at file scope:

```c
/* USER CODE BEGIN 4 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    APP_UART_OnRxByte(huart);
}
/* USER CODE END 4 */
```

> Make sure `BSP_COM_Init` runs **before** `APP_FEC_Init` — the banner
> is printed inside `APP_FEC_Init` and needs the VCP already up.

## 7. Build and flash

- **Project → Build All** (Ctrl-B). Zero errors expected.
- **Run → Run** (Ctrl-F11). ST-LINK flashes the CM7 image.

## 8. Open the terminal

Pick any serial monitor (CubeIDE's built-in, PuTTY, Tera Term, `minicom`).

- Port: the ST-LINK VCP (COMx on Windows, `/dev/ttyACMx` on Linux)
- Baud: **115200**, 8N1, no flow control

You should see the banner:

```
==================================================
STeRG S1.0 FEC on-board test — NUCLEO-H755ZI-Q
  UHF HK    : RS(255,223) + Conv 1/2 K=7
  UHF Beacon: RS(255,223) shortened
  Listening on MP257 UART at 115200 8N1
==================================================
```

Start the MP257 cFS image. Per-frame log lines start scrolling:

```
[0001] HK  mid=0x089C len= 48  enc=512 B  rt=OK    4 ms
[0002] HK  mid=0x089D len=132  enc=512 B  rt=OK    4 ms
...
        --- totals: 32 ok, 0 fail of 32 ---
```

`rt=OK` = input zero-padded to 223 B → RS → Conv → Viterbi → RS decode
→ byte-for-byte match against the original.

## 9. Troubleshooting

| Symptom | Likely cause |
|---|---|
| Banner prints, then silence | MP257 not transmitting, or TX↔RX swapped |
| `APP_FEC_Init: allocation failed` | `_Min_Heap_Size` too small — needs ≥ `0x8000` |
| `??  mid=0x???? len=N UNKNOWN` | Framing drift; confirm both ends at 115200 and MP257 really sends unscrambled |
| `rt=FAIL` every frame | Data corruption before FEC — check baud / common ground |
| No banner either | USART3 BSP not initialised — move `BSP_COM_Init` above `APP_FEC_Init` |
| Banner OK but no frames | USART2 global interrupt not enabled in CubeMX NVIC |

## 10. Next steps

- Flip Conv soft decoding on by switching to `sterg_uhf_hk_decode_soft`
  (not yet implemented — straight forward to add once the DAC captures
  soft symbols from the Si4464).
- Forward the Conv-encoded stream out of a third UART to the Si4464
  evaluation board for true end-to-end RF testing.
