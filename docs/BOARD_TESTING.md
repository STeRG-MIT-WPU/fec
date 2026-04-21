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
                            H755  USART3 TX
                                   │
                            (ST-LINK USB VCP)
                                   │
                                   ▼
                               Host PC terminal
```

## 1. Wiring

MP257 (currently outputs raw CCSDS at 115200 8N1 on `/dev/ttyUSB0`, see
`apps/sterg_uhf_app/fsw/src/sterg_uhf_app.c`) → H755-ZI-Q:

| MP257 | H755 pin | Connector | Role |
|---|---|---|---|
| UART TX  | **PD6** (USART2_RX) | CN9-4 | data in |
| GND      | **GND**             | any ground pin | reference |
| _(optional)_ UART RX | PD5 (USART2_TX) | CN9-6 | back-channel |

USART3 (ST-LINK VCP) is wired internally — nothing to connect, just the
USB-Micro lead to your PC. VCP appears as `COMx` on Windows or
`/dev/ttyACM0` on Linux.

## 2. Create the CubeIDE project

1. **File → New → STM32 Project**, select board **NUCLEO-H755ZI-Q**,
   targeted firmware **Cortex-M7** (CM4 stays in stop mode — not used).
2. In the CubeMX perspective, open **Pinout & Configuration**:

   **USART3** (VCP):
   - Mode: **Asynchronous**
   - Parameters: **115200 Bd, 8 bits, no parity, 1 stop bit**

   **USART2** (MP257 link):
   - Mode: **Asynchronous**
   - Pins: check that **PD5 = USART2_TX** and **PD6 = USART2_RX** are
     selected (they're the defaults on the ZI-Q).
   - Parameters: **115200 Bd, 8N1**
   - **NVIC Settings** tab → enable **USART2 global interrupt**.

3. **Project → Properties → C/C++ Build → Settings → MCU Settings**:
   check that **Use float with printf** is **off** (saves flash).

4. Open the generated linker script (`STM32H755ZITX_FLASH.ld` under
   `CM7/`) and bump:
   ```
   _Min_Heap_Size  = 0x8000;   /* 32 KB — decoder needs ~25 KB  */
   _Min_Stack_Size = 0x1000;   /* 4 KB                           */
   ```

5. Click **Generate Code** (the little gear icon or Project → Generate).

## 3. Drop in `fec_sterg`

1. Copy the entire `fec_sterg/` folder into the root of the generated
   project (next to `CM7/`, `CM4/`, etc.).
2. Right-click the **CM7** sub-project → **Properties**:
   - **C/C++ General → Paths and Symbols → Includes → GNU C**, add:
     ```
     ../../fec_sterg/Inc
     ../../fec_sterg/app
     ```
   - **Source Location**, add the folder `/<project>/fec_sterg` and
     uncheck the `docker`, `docs`, `example`, `test`, `obj` sub-folders
     (keep **Inc**, **Src**, **app**).

3. Clean and build. You should get zero errors.

## 4. Hook the app into `main.c` (CM7)

Add the highlighted lines to the auto-generated `CM7/Core/Src/main.c`:

```c
/* USER CODE BEGIN Includes */
#include "app_fec.h"
#include "app_uart.h"
/* USER CODE END Includes */

...

int main(void) {
    /* ... all the HAL_Init / SystemClock_Config / MX_*_Init() lines ... */

    /* USER CODE BEGIN 2 */
    if (APP_FEC_Init(&huart2, &huart3) != 0) {
        /* Heap too small — bring the linker script in line and re-flash */
        Error_Handler();
    }
    /* USER CODE END 2 */

    while (1) {
        /* USER CODE BEGIN WHILE */
        APP_FEC_Loop();
        /* USER CODE END WHILE */
    }
}

/* USER CODE BEGIN 4 */
/* Called by HAL from the USART2 ISR whenever one byte has arrived. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    APP_UART_OnRxByte(huart);
}
/* USER CODE END 4 */
```

> Put `APP_FEC_Init` and the HAL init `MX_USART*_Init()` calls in the
> right order: MX_USART3_UART_Init and MX_USART2_UART_Init must run
> **before** APP_FEC_Init.

## 5. Build and flash

- **Project → Build All** (Ctrl-B).
- **Run → Debug** (F11) — the built-in ST-LINK flashes the CM7 image
  and halts at `main()`. Press Resume (F8).

## 6. Open the terminal

Pick your favourite serial monitor (CubeIDE's built-in one, PuTTY,
`minicom`, `screen`, `pyserial-miniterm`). Settings:

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

Then start the MP257 cFS image. As it emits combined HK packets you'll
see lines like:

```
[0001] HK  mid=0x089C len= 48  enc=512 B  rt=OK    4 ms
[0002] HK  mid=0x089D len=132  enc=512 B  rt=OK    4 ms
[0003] HK  mid=0x089C len= 48  enc=512 B  rt=OK    4 ms
...
        --- totals: 32 ok, 0 fail of 32 ---
```

`rt=OK` means: input zero-padded to 223 B → RS-encoded → Conv-encoded
→ Viterbi-decoded → RS-decoded → byte-for-byte match against the
original. That's the full round-trip proving the H755 runs the codec
correctly on real telemetry.

## 7. Troubleshooting

| Symptom | Likely cause |
|---|---|
| Banner prints, then silence | MP257 not transmitting, or TX↔RX swapped on the wire |
| `APP_FEC_Init: allocation failed` | `_Min_Heap_Size` too small — needs ≥ `0x8000` |
| `??  mid=0x???? len=N UNKNOWN` repeatedly | Framing drift; confirm both ends at 115200, and that the MP257 output really is unscrambled (check `sterg_uhf_app.c` — no `STERG_Scramble_Apply` call) |
| `rt=FAIL` on every frame | Data reaching H755 is corrupted before FEC — check baud / common ground |
| Encode time > 10 ms | You're on CM4 at 240 MHz, fine. CM7 at 480 MHz should be ~3-4 ms |

## 8. Next steps

- Flip Conv soft decoding on by switching to `sterg_uhf_hk_decode_soft`
  (not yet implemented — straight forward to add once the DAC captures
  soft symbols from the Si4464).
- Forward the Conv-encoded stream out of a third UART to the Si4464
  evaluation board for true end-to-end RF testing.
- Measure heap high-water by enabling `MALLOC_DEBUG` or linking
  `newlib_mallinfo`.
