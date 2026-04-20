# Docker-based verification for fec_sterg

The image bundles a Linux `gcc` for host-side functional tests and
`arm-none-eabi-gcc` for an STM32-target cross-compile sanity check, so
you can validate the port without installing any toolchain on the host.

## Prerequisites

- Docker Desktop running (Windows/Mac) or `dockerd` active (Linux).

## One-shot run

From the repo root (`D:\STeRG\FEC`):

```powershell
docker build -t fec_sterg_verify -f fec_sterg/docker/Dockerfile fec_sterg/docker
docker run --rm -v ${PWD}:/work fec_sterg_verify
```

Expected output (abbreviated):

```
==> Running host test harness
fec_sterg verification harness
==============================
[01] RS(255,247) clean round-trip                         PASS
[02] RS(255,239) clean round-trip                         PASS
[03] RS(255,223) clean round-trip                         PASS
[04] RS(255,247) recovers 4 byte errors                   PASS
[05] RS(255,239) recovers 8 byte errors                   PASS
[06] RS(255,223) recovers 16 byte errors                  PASS
[07] RS(255,223) returns -1 with too many errors          PASS
[08] RS(255,223) decode_with_erasures recovers 32 erasures PASS
[09] Conv r=1/2 o=7 round-trip + 3 bit flips              PASS
[10] Conv r=1/3 o=7 round-trip + 3 bit flips              PASS
[11] Conv r=1/2 o=9 round-trip + 3 bit flips              PASS
[12] Conv r=1/2 o=7 soft decoding (AWGN-ish)              PASS

12/12 tests passed
==> Cross-compile OK: 12 Cortex-M4 objects built
```

A non-zero exit code means at least one test failed.

## Selective runs

Drop into the container and run individual targets:

```powershell
docker run --rm -it -v ${PWD}:/work fec_sterg_verify bash
# inside the container:
make -C fec_sterg/test host-test    # run only the functional tests
make -C fec_sterg/test arm-build    # run only the STM32 cross-compile
make -C fec_sterg/test clean
```

## What gets tested

**Host (gcc, `host-test`):** 12 scenarios covering RS at several
`num_roots`, error correction at the theoretical limit, clean failure
past the limit, erasure decoding, convolutional hard-decision decoding
at rates 1/2 and 1/3 with orders 7 and 9, and soft-decision decoding.

**ARM (`arm-build`):** every source file is compiled with:

```
-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -Os
```

If the port compiles clean here, it compiles clean in STM32CubeIDE with
the default F4/G4/H7 presets. Linking is skipped because it requires a
target-specific linker script and startup object.

## Using a different Cortex core

Edit `ARM_CFLAGS` in `fec_sterg/test/Makefile`:

| Target series | Flags |
|---|---|
| Cortex-M0/M0+ (F0, G0, L0) | `-mcpu=cortex-m0 -mthumb` (no FPU) |
| Cortex-M3 (F1, F2, L1)     | `-mcpu=cortex-m3 -mthumb` |
| Cortex-M4F (F3, F4, G4, L4)| `-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard` |
| Cortex-M7 (F7, H7)         | `-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard` |
