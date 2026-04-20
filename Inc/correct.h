/*
 * fec_sterg - Forward Error Correction library ported for STM32
 * Based on libcorrect (https://github.com/quiet/libcorrect)
 * Licensed under BSD-3-Clause (see LICENSE)
 */
#ifndef CORRECT_H
#define CORRECT_H

#include <stdint.h>
#include <stddef.h>

/* STM32 port: unistd.h is not available in arm-none-eabi newlib-nano.
 * ssize_t is provided as ptrdiff_t for portability across toolchains
 * (arm-gcc, Keil MDK, IAR). */
#if !defined(__ssize_t_defined) && !defined(_SSIZE_T_DECLARED) && !defined(_SSIZE_T_DEFINED)
typedef ptrdiff_t ssize_t;
#define _SSIZE_T_DEFINED
#endif


// Convolutional Codes

// Convolutional polynomials are 16 bits wide
typedef uint16_t correct_convolutional_polynomial_t;

static const correct_convolutional_polynomial_t correct_conv_r12_6_polynomial[] = {073, 061};
static const correct_convolutional_polynomial_t correct_conv_r12_7_polynomial[] = {0161, 0127};
static const correct_convolutional_polynomial_t correct_conv_r12_8_polynomial[] = {0225, 0373};
static const correct_convolutional_polynomial_t correct_conv_r12_9_polynomial[] = {0767, 0545};
static const correct_convolutional_polynomial_t correct_conv_r13_6_polynomial[] = {053, 075, 047};
static const correct_convolutional_polynomial_t correct_conv_r13_7_polynomial[] = {0137, 0153,
                                                                                   0121};
static const correct_convolutional_polynomial_t correct_conv_r13_8_polynomial[] = {0333, 0257,
                                                                                   0351};
static const correct_convolutional_polynomial_t correct_conv_r13_9_polynomial[] = {0417, 0627,
                                                                                   0675};

/* CCSDS 131.0-B-5 convolutional code — K=7, rate 1/2.
 *
 * The CCSDS standard specifies (using its own bit-ordering):
 *   G1 = 171 octal   (1 + x + x^2 + x^3 + x^6)
 *   G2 = 133 octal   (1 + x^2 + x^3 + x^5 + x^6)
 *
 * libcorrect represents polynomials with bit k = tap at delay D^k (LSB-first),
 * which is the bit-reverse of CCSDS's MSB-first convention. The values below
 * are the CCSDS polynomials in libcorrect's bit order — identical to Phil
 * Karn's libfec V27POLYA/V27POLYB used by essentially every ground-station
 * CCSDS decoder.
 *
 * NOTE: CCSDS also specifies that the G2 output be inverted (1's-complement)
 * to avoid all-zero output on all-zero input. libcorrect does not invert
 * automatically; for strict CCSDS interop, XOR every second output bit with 1
 * after encoding, and do the reverse on the demodulated stream before
 * decoding. For in-library round-trip use, the inversion is unnecessary.
 */
static const correct_convolutional_polynomial_t correct_conv_ccsds_r12_7_polynomial[] = {0155, 0117};

typedef uint8_t correct_convolutional_soft_t;

struct correct_convolutional;
typedef struct correct_convolutional correct_convolutional;

/* correct_convolutional_create allocates and initializes an encoder/decoder for
 * a convolutional code with the given parameters. This function expects that
 * poly will contain inv_rate elements.
 *
 * Returns a non-NULL pointer on success. Heap allocation is used — make sure
 * the STM32 linker-script heap is large enough (>= ~16 KB recommended for
 * decoder at rate=2, order=7).
 */
correct_convolutional *correct_convolutional_create(size_t inv_rate, size_t order,
                                                    const correct_convolutional_polynomial_t *poly);

/* correct_convolutional_destroy releases all resources associated with conv. */
void correct_convolutional_destroy(correct_convolutional *conv);

/* correct_convolutional_encode_len returns the number of *bits* in a msg_len
 * of given size, in *bytes*. */
size_t correct_convolutional_encode_len(correct_convolutional *conv, size_t msg_len);

/* correct_convolutional_encode encodes a block of data. Returns number of
 * bits written to encoded. */
size_t correct_convolutional_encode(correct_convolutional *conv, const uint8_t *msg, size_t msg_len,
                                    uint8_t *encoded);

/* correct_convolutional_decode decodes a block using viterbi. Returns number
 * of bytes written on success, or -1 on failure. */
ssize_t correct_convolutional_decode(correct_convolutional *conv, const uint8_t *encoded,
                                     size_t num_encoded_bits, uint8_t *msg);

/* correct_convolutional_decode_soft decodes a block encoded and modulated to
 * 8-bit soft symbols. 1 -> 255, 0 -> 0, erased -> 128. */
ssize_t correct_convolutional_decode_soft(correct_convolutional *conv,
                                          const correct_convolutional_soft_t *encoded,
                                          size_t num_encoded_bits, uint8_t *msg);

// Reed-Solomon

struct correct_reed_solomon;
typedef struct correct_reed_solomon correct_reed_solomon;

static const uint16_t correct_rs_primitive_polynomial_8_4_3_2_0 =
    0x11d;  // x^8 + x^4 + x^3 + x^2 + 1

static const uint16_t correct_rs_primitive_polynomial_8_5_3_1_0 =
    0x12b;  // x^8 + x^5 + x^3 + x + 1

static const uint16_t correct_rs_primitive_polynomial_8_5_3_2_0 =
    0x12d;  // x^8 + x^5 + x^3 + x^2 + 1

static const uint16_t correct_rs_primitive_polynomial_8_6_3_2_0 =
    0x14d;  // x^8 + x^6 + x^3 + x^2 + 1

static const uint16_t correct_rs_primitive_polynomial_8_6_4_3_2_1_0 =
    0x15f;  // x^8 + x^6 + x^4 + x^3 + x^2 + x + 1;

static const uint16_t correct_rs_primitive_polynomial_8_6_5_1_0 =
    0x163;  // x^8 + x^6 + x^5 + x + 1

static const uint16_t correct_rs_primitive_polynomial_8_6_5_2_0 =
    0x165;  // x^8 + x^6 + x^5 + x^2 + 1

static const uint16_t correct_rs_primitive_polynomial_8_6_5_3_0 =
    0x169;  // x^8 + x^6 + x^5 + x^3 + 1

static const uint16_t correct_rs_primitive_polynomial_8_6_5_4_0 =
    0x171;  // x^8 + x^6 + x^5 + x^4 + 1

static const uint16_t correct_rs_primitive_polynomial_8_7_2_1_0 =
    0x187;  // x^8 + x^7 + x^2 + x + 1

static const uint16_t correct_rs_primitive_polynomial_8_7_3_2_0 =
    0x18d;  // x^8 + x^7 + x^3 + x^2 + 1

static const uint16_t correct_rs_primitive_polynomial_8_7_5_3_0 =
    0x1a9;  // x^8 + x^7 + x^5 + x^3 + 1

static const uint16_t correct_rs_primitive_polynomial_8_7_6_1_0 =
    0x1c3;  // x^8 + x^7 + x^6 + x + 1

static const uint16_t correct_rs_primitive_polynomial_8_7_6_3_2_1_0 =
    0x1cf;  // x^8 + x^7 + x^6 + x^3 + x^2 + x + 1

static const uint16_t correct_rs_primitive_polynomial_8_7_6_5_2_1_0 =
    0x1e7;  // x^8 + x^7 + x^6 + x^5 + x^2 + x + 1

static const uint16_t correct_rs_primitive_polynomial_8_7_6_5_4_2_0 =
    0x1f5;  // x^8 + x^7 + x^6 + x^5 + x^4 + x^2 + 1

static const uint16_t correct_rs_primitive_polynomial_ccsds =
    0x187;  // x^8 + x^7 + x^2 + x + 1

/* correct_reed_solomon_create allocates and initializes an
 * encoder/decoder for a given reed solomon error correction
 * code. Block size is always 255 bytes with 8-bit symbols. */
correct_reed_solomon *correct_reed_solomon_create(uint16_t primitive_polynomial,
                                                  uint8_t first_consecutive_root,
                                                  uint8_t generator_root_gap,
                                                  size_t num_roots);

/* correct_reed_solomon_encode writes encoded block to `encoded`. */
ssize_t correct_reed_solomon_encode(correct_reed_solomon *rs, const uint8_t *msg, size_t msg_length,
                                    uint8_t *encoded);

/* correct_reed_solomon_decode attempts to recover payload from a possibly
 * corrupted block. Returns bytes written to msg or -1 on failure. */
ssize_t correct_reed_solomon_decode(correct_reed_solomon *rs, const uint8_t *encoded,
                                    size_t encoded_length, uint8_t *msg);

/* correct_reed_solomon_decode_with_erasures adds known erasure locations. */
ssize_t correct_reed_solomon_decode_with_erasures(correct_reed_solomon *rs, const uint8_t *encoded,
                                                  size_t encoded_length,
                                                  const uint8_t *erasure_locations,
                                                  size_t erasure_length, uint8_t *msg);

/* correct_reed_solomon_destroy releases the resources associated with rs. */
void correct_reed_solomon_destroy(correct_reed_solomon *rs);

#endif
