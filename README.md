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

## High-level API — `sterg_fec.h`

```c
#include "sterg_fec.h"

/* ---- UHF HK: 223 B in → 4096 bits out ---- */
sterg_uhf_hk_t *hk = sterg_uhf_hk_create();

uint8_t msg[STERG_HK_MSG_LEN];        // 223 B
uint8_t enc[STERG_HK_ENCODED_MAX_LEN]; // 512 B
uint8_t dec[STERG_HK_MSG_LEN];

size_t bits = sterg_uhf_hk_encode(hk, msg, enc);     // always 4096
int rc = sterg_uhf_hk_decode(hk, enc, bits, dec);    // 0 == ok

sterg_uhf_hk_destroy(hk);

/* ---- UHF Beacon: 5 modes, 89..99 B in → +32 B parity ---- */
sterg_uhf_beacon_t *bcn = sterg_uhf_beacon_create();

uint8_t beacon[STERG_BEACON_MAX_PAYLOAD];
uint8_t enc[STERG_BEACON_MAX_ENCODED];

size_t n = sterg_uhf_beacon_encode(bcn,
            STERG_BEACON_MODE_NOMINAL, beacon, enc); // 99+32 = 131 B

uint8_t out[STERG_BEACON_MAX_PAYLOAD];
ssize_t got = sterg_uhf_beacon_decode(bcn,
            STERG_BEACON_MODE_NOMINAL, enc, out);    // 99 on success

sterg_uhf_beacon_destroy(bcn);
```

### Beacon modes (from FEC-FINAL §5.5)

| Mode | Name | Payload | RS code | On-air |
|---|---|---|---|---|
| 0 | Nominal    | 99 B | RS(131,99) | 131 B |
| 1 | Pre-link   | 96 B | RS(128,96) | 128 B |
| 2 | Detumbling | 89 B | RS(121,89) | 121 B |
| 3 | Low power  | 95 B | RS(127,95) | 131 B |
| 4 | Critical   | 89 B | RS(121,89) | 121 B |

All modes use the same GF(256) encoder with 32 parity bytes — every
mode still corrects up to 16 byte errors.

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
> the G2 output to prevent all-zero-in → all-zero-out. libcorrect does
> not do this automatically. For bit-exact interop with a CCSDS ground
> station, XOR every second output bit with 1 after encoding (and undo
> on receive). In-library round-trip encode/decode works either way.

## Low-level API (still available)

`correct.h` preserves the full libcorrect API if you need a
non-standard code. A new polynomial constant has been added:

```c
static const correct_convolutional_polynomial_t
    correct_conv_ccsds_r12_7_polynomial[] = {0155, 0117};
```

## Folder layout

```
fec_sterg/
├── Inc/
│   ├── correct.h                   # libcorrect public API + CCSDS conv poly
│   ├── sterg_fec.h                 # STeRG high-level API (HK + Beacon)
│   └── correct/                    # internal libcorrect headers
├── Src/
│   ├── convolutional/              # Viterbi + convolutional encoder
│   ├── reed-solomon/               # RS(255,223) codec
│   └── sterg/sterg_fec.c           # HK and Beacon chains
├── example/fec_example.c           # callable demo
├── test/                           # verification harness (14 tests)
├── docker/                         # reproducible build env
└── docs/ … FEC_FINAL_DECISION.pdf  # source-of-truth spec (in repo root)
```

## STM32CubeIDE integration

1. Drop `fec_sterg/` into your project.
2. **Includes**: add `../fec_sterg/Inc`.
3. **Source Location**: add `/<project>/fec_sterg/Src` (all three subdirs
   are picked up automatically).
4. Bump the linker heap:
   ```
   _Min_Heap_Size  = 0x8000;   /* 32 KB — decoder needs ~25 KB */
   _Min_Stack_Size = 0x1000;
   ```
5. From `main()`:
   ```c
   #include "sterg_fec.h"
   int fec_sterg_example_run(void);   /* from example/fec_example.c */
   ```

## Memory footprint

Heap (allocated lazily on first decode, then reused):

| Chain | Heap usage |
|---|---|
| UHF HK (RS + Conv decoder)  | ~28 KB |
| UHF Beacon (RS only)        | ~18 KB |

Flash: ~12-15 KB `.text` for both chains combined on Cortex-M4 at `-Os`.

## Docker verification

See `docker/README.md`. Short version:

```powershell
docker build -t fec_sterg_verify -f fec_sterg/docker/Dockerfile fec_sterg/docker
docker run --rm -v ${PWD}:/work fec_sterg_verify
```

Runs 14 functional tests on the host plus an `arm-none-eabi-gcc`
Cortex-M4 cross-compile.

## License

BSD-3-Clause (same as upstream libcorrect).
