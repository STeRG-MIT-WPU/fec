# fec_sterg — STeRG S1.0 UHF FEC for STM32

Forward-Error-Correction library implementing the two UHF coding chains
mandated by the **STeRG S1.0 FEC Final Decision** (FEC-FINAL-002). Ported
from [quiet/libcorrect](https://github.com/quiet/libcorrect) and stripped
of desktop/SSE dependencies so it compiles clean with `arm-none-eabi-gcc`.

## What's in the box

| Link | Chain | Standard | Code rate |
|---|---|---|---|
| **UHF HK** (VCID 0) | RS(255,223) + Conv 1/2 K=7 | CCSDS 131.0-B-5 | 0.437 |
| **UHF Beacon** (VCID 7) | RS(255,223) shortened (5 modes) | CCSDS 131.0-B-5 §4 | ≈0.75 |

S-band LDPC 1/2 lives on the Zynq-7000 and is **not** part of this
library.

## STM32CubeIDE integration — the easy way

Everything you need is under two flat folders. Open File Explorer, select
all files inside each folder, and drag-and-drop them into the matching
folder inside your CubeIDE project. That's it — no include-path fiddling,
no linked folders.

| Copy all files from… | …into |
|---|---|
| `fec_sterg/Inc/*.h` | `<your-project>/Core/Inc/` |
| `fec_sterg/Src/*.c` | `<your-project>/Core/Src/` |

CubeIDE automatically picks up everything under `Core/Inc` and `Core/Src`.

Then:

1. Bump the heap in your linker script (`STM32xxxx_FLASH.ld`):
   ```
   _Min_Heap_Size  = 0x8000;     /* 32 KB — decoder needs ~25 KB   */
   _Min_Stack_Size = 0x1000;
   ```
2. In `main.c`, after HAL init:
   ```c
   #include "app_fec.h"
   #include "app_uart.h"

   int main(void) {
       /* … HAL_Init / SystemClock_Config / MX_USART2_UART_Init() … */

       /* BSP_COM_Init first so printf has a sink (NUCLEO-H755ZI-Q) */
       BspCOMInit.BaudRate = 115200;
       /* … */
       BSP_COM_Init(COM1, &BspCOMInit);

       APP_FEC_Init(&huart2);

       while (1) {
           APP_FEC_Loop();
       }
   }

   /* Wire the RX interrupt callback */
   void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
       APP_UART_OnRxByte(huart);
   }
   ```
3. In CubeMX, enable **USART2** → Asynchronous → 115200 8N1 →
   **NVIC: USART2 global interrupt ON**. USART3 (VCP) stays under BSP
   control.

See [docs/BOARD_TESTING.md](docs/BOARD_TESTING.md) for the full
NUCLEO-H755ZI-Q walk-through (wiring, expected output, troubleshooting).

## High-level API — `sterg_fec.h`

```c
#include "sterg_fec.h"

/* ---- UHF HK: 223 B in → 4096 bits out ---- */
sterg_uhf_hk_t *hk = sterg_uhf_hk_create();

uint8_t msg[STERG_HK_MSG_LEN];        // 223 B
uint8_t enc[STERG_HK_ENCODED_MAX_LEN]; // 512 B
uint8_t dec[STERG_HK_MSG_LEN];

size_t bits = sterg_uhf_hk_encode(hk, msg, enc);  // 4096
int rc = sterg_uhf_hk_decode(hk, enc, bits, dec); // 0 == ok

sterg_uhf_hk_destroy(hk);

/* ---- UHF Beacon: 5 modes ---- */
sterg_uhf_beacon_t *bcn = sterg_uhf_beacon_create();
uint8_t enc[STERG_BEACON_MAX_ENCODED];
uint8_t out[STERG_BEACON_MAX_PAYLOAD];

size_t n = sterg_uhf_beacon_encode(bcn,
             STERG_BEACON_MODE_NOMINAL, payload, enc);   /* 131 B */
ssize_t g = sterg_uhf_beacon_decode(bcn,
             STERG_BEACON_MODE_NOMINAL, enc, out);       /* 99 on success */

sterg_uhf_beacon_destroy(bcn);
```

### Beacon modes (from FEC-FINAL §5.5)

| Mode | Name | Payload | RS code |
|---|---|---|---|
| 0 | Nominal    | 99 B | RS(131,99) |
| 1 | Pre-link   | 96 B | RS(128,96) |
| 2 | Detumbling | 89 B | RS(121,89) |
| 3 | Low power  | 95 B | RS(127,95) |
| 4 | Critical   | 89 B | RS(121,89) |

Every mode uses 32 parity bytes — corrects up to 16 byte errors.

## CCSDS parameters used internally

| Parameter | Value |
|---|---|
| RS primitive polynomial | `0x187` = `x^8 + x^7 + x^2 + x + 1` |
| RS first consecutive root | 112 |
| RS generator root gap | 11 |
| RS parity bytes (2t) | 32 |
| Conv constraint length K | 7 |
| Conv rate | 1/2 |
| Conv polynomials (libcorrect bit order) | `{0155, 0117}` |
| Conv polynomials (CCSDS doc notation) | `G1 = 0171, G2 = 0133` |

> **CCSDS G2 inversion:** The CCSDS 131.0-B-5 spec mandates inverting
> the G2 output to avoid all-zero-in → all-zero-out. libcorrect does
> not do this automatically. For bit-exact interop with a CCSDS ground
> station, XOR every second output bit with 1 after encoding (and undo
> on receive). In-library round-trip encode/decode works either way.

## Low-level API (still available)

`correct.h` preserves the full libcorrect API if you need a non-standard
code. A new polynomial constant has been added:

```c
static const correct_convolutional_polynomial_t
    correct_conv_ccsds_r12_7_polynomial[] = {0155, 0117};
```

## Folder layout

```
fec_sterg/
├── Inc/                        # ◄ copy contents into Core/Inc/
│   ├── correct.h               #   libcorrect public API
│   ├── sterg_fec.h             #   STeRG high-level API
│   ├── app_fec.h               #   board test app
│   ├── app_uart.h              #   RX ring buffer
│   ├── correct_portable.h      #   popcount / prefetch shims
│   ├── conv_types.h            #   convolutional low-level types
│   ├── conv_internal.h         #   correct_convolutional struct
│   ├── conv_bit.h              #   bit-stream I/O
│   ├── conv_metric.h           #   hamming / soft distance
│   ├── conv_lookup.h           #   polynomial lookup tables
│   ├── conv_error_buffer.h     #   Viterbi error-metric ring
│   ├── conv_history_buffer.h   #   Viterbi traceback
│   ├── rs_types.h              #   Reed-Solomon types + struct
│   ├── rs_field.h              #   GF(2^8) arithmetic
│   └── rs_polynomial.h         #   polynomial ops
│
├── Src/                        # ◄ copy contents into Core/Src/
│   ├── app_fec.c               #   board app: CCSDS reassembly + round-trip
│   ├── app_uart.c              #   IRQ-driven RX ring buffer
│   ├── sterg_fec.c             #   STeRG chain implementations
│   ├── fec_conv.c              #   convolutional encoder ctor/dtor
│   ├── fec_conv_bit.c          #   bit writer/reader
│   ├── fec_conv_encode.c       #   encoder
│   ├── fec_conv_decode.c       #   Viterbi decoder
│   ├── fec_conv_metric.c       #   quadratic soft metric
│   ├── fec_conv_lookup.c       #   table fill / pair lookup
│   ├── fec_conv_error_buffer.c #   error metric ring
│   ├── fec_conv_history_buffer.c
│   ├── fec_rs.c                #   RS ctor/dtor
│   ├── fec_rs_encode.c
│   ├── fec_rs_decode.c
│   └── fec_rs_polynomial.c
│
├── example/fec_example.c       # callable demo
├── test/                       # verification harness (11 tests)
├── docker/                     # reproducible build environment
└── docs/                       # board-testing guide
```

## Memory footprint

Heap (allocated lazily on first decode call, then reused):

| Chain | Heap usage |
|---|---|
| UHF HK (RS + Conv decoder) | ~28 KB |
| UHF Beacon (RS only)       | ~18 KB |

Flash: ~12-15 KB `.text` for both chains combined on Cortex-M4 at `-Os`.

## Docker verification

See `docker/README.md`. Short version:

```powershell
docker build -t fec_sterg_verify -f fec_sterg/docker/Dockerfile fec_sterg/docker
docker run --rm -v ${PWD}:/work fec_sterg_verify
```

Runs 11 functional tests on the host plus an `arm-none-eabi-gcc`
Cortex-M4 cross-compile.

## License

BSD-3-Clause (same as upstream libcorrect).
