# fec_sterg — libcorrect port for STM32

Forward-Error-Correction library for STM32 microcontrollers, ported from
[quiet/libcorrect](https://github.com/quiet/libcorrect).

Provides two codecs:

- **Reed-Solomon** — systematic RS(255, 255-2t), recovers up to `t` byte errors.
- **Convolutional + Viterbi** — rate 1/2 or 1/3 with configurable order and
  polynomials. Supports hard and soft (8-bit) decoding.

## What changed vs. upstream

| Change | Why |
|---|---|
| `<unistd.h>` removed; `ssize_t` always `ptrdiff_t` | arm-none-eabi newlib-nano lacks `unistd.h` |
| All `<stdio.h>` / `printf` debug code stripped | No semihosting required; saves flash |
| `<time.h>`, `<assert.h>` dropped from headers | Not needed on bare metal |
| All SSE-specific sources excluded | No SIMD on Cortex-M |
| `popcount` portable fallback fixed (paren bug) | Original macro had operator-precedence bug |
| `popcount` enabled for `__clang__` too | For ARMClang v6 toolchains |
| Small NULL-check hardening on `malloc` callers | Fail gracefully on exhausted heap |

The public API in `correct.h` is **unchanged** — code that already uses
libcorrect will link against fec_sterg without modification.

## Folder layout

```
fec_sterg/
├── Inc/                        # add this to your compiler include path
│   ├── correct.h               # public API
│   └── correct/
│       ├── portable.h
│       ├── convolutional.h
│       ├── reed-solomon.h
│       ├── convolutional/…     # internal headers
│       └── reed-solomon/…
├── Src/
│   ├── convolutional/          # 7 .c files
│   └── reed-solomon/           # 4 .c files
└── example/
    └── fec_example.c           # quick sanity demo
```

## Integration into STM32CubeIDE

1. Copy the `fec_sterg/` folder into your project (next to `Core/`).
2. Right-click the project → *Properties* → *C/C++ General* → *Paths and Symbols*:
   - **Includes**: add `../fec_sterg/Inc`
   - **Source Location**: add `/<project>/fec_sterg/Src`
3. Edit the linker script (`STM32xxxx_FLASH.ld`) and bump the heap:
   ```
   _Min_Heap_Size  = 0x8000;   /* 32 KB — required for the decoder */
   _Min_Stack_Size = 0x1000;
   ```
4. In `main.c`:
   ```c
   #include "correct.h"

   int fec_sterg_example_run(void); /* from example/fec_example.c */

   int main(void) {
       HAL_Init();
       SystemClock_Config();
       MX_GPIO_Init();

       if (fec_sterg_example_run() == 0) {
           HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
       }
       while (1) { }
   }
   ```

## Integration via Makefile (arm-none-eabi-gcc)

```make
CFLAGS  += -Ifec_sterg/Inc
SOURCES += $(wildcard fec_sterg/Src/convolutional/*.c) \
           $(wildcard fec_sterg/Src/reed-solomon/*.c)
```

## Memory footprint

Heap usage (worst case, allocated on first decode call and reused):

| Codec | Config | Approx. heap |
|---|---|---|
| Convolutional decoder | rate=2, order=7 | ~10 KB |
| Convolutional decoder | rate=2, order=9 | ~40 KB |
| Reed-Solomon decoder  | num_roots=32    | ~18 KB |

Flash/code size is around **10–15 KB** for both codecs combined on Cortex-M4
with `-Os`.

**Minimum target:** an STM32 with ≥ 32 KB SRAM (F103C8, F401RE, F411RE,
F446RE, G474, H7, etc.). F030/F072 class parts (< 20 KB SRAM) will not
fit the default decoder allocations — use RS only with smaller
`num_roots`, or replace `malloc` with a static-pool allocator.

## Thread / interrupt safety

No static mutable state is touched on the encode/decode paths after
`correct_*_create()` returns. Separate instances can run concurrently
on different RTOS tasks. A single instance is **not** safe to share
across tasks without external locking.

Do **not** call encode/decode from an ISR — the decoder may execute
tens of thousands of instructions.

## License

BSD-3-Clause (same as upstream libcorrect). See the original repo for
the full text.
