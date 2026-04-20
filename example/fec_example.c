/*
 * fec_sterg example for STM32.
 *
 * Demonstrates both Reed-Solomon and convolutional (Viterbi) codec usage.
 * Call fec_sterg_example_run() from main() after HAL init. The result
 * is returned as a bitfield; on an STM32 Nucleo you can light the LED
 * if it returns 0.
 *
 * Requires ~32 KB heap (_Min_Heap_Size in linker script).
 */
#include <string.h>
#include <stdint.h>

#include "correct.h"

#define RS_EXAMPLE_MSG_LEN   223
#define RS_EXAMPLE_PARITY    32   /* block = 255 */

#define CONV_EXAMPLE_MSG_LEN 32

static int rs_example(void) {
    uint8_t msg[RS_EXAMPLE_MSG_LEN];
    uint8_t encoded[RS_EXAMPLE_MSG_LEN + RS_EXAMPLE_PARITY];
    uint8_t decoded[RS_EXAMPLE_MSG_LEN];

    for (size_t i = 0; i < RS_EXAMPLE_MSG_LEN; i++) {
        msg[i] = (uint8_t)(i * 37u + 11u);
    }

    correct_reed_solomon *rs = correct_reed_solomon_create(
        correct_rs_primitive_polynomial_ccsds, 1, 1, RS_EXAMPLE_PARITY);
    if (!rs) return 1;

    ssize_t enc_len = correct_reed_solomon_encode(rs, msg, RS_EXAMPLE_MSG_LEN, encoded);
    if (enc_len < 0) {
        correct_reed_solomon_destroy(rs);
        return 2;
    }

    /* Simulate errors: flip 16 bytes (up to num_roots/2 is recoverable) */
    for (int i = 0; i < 16; i++) {
        encoded[i * 7] ^= 0xA5;
    }

    ssize_t dec_len = correct_reed_solomon_decode(rs, encoded, enc_len, decoded);
    correct_reed_solomon_destroy(rs);

    if (dec_len != RS_EXAMPLE_MSG_LEN) return 3;
    if (memcmp(msg, decoded, RS_EXAMPLE_MSG_LEN) != 0) return 4;

    return 0;
}

static int conv_example(void) {
    uint8_t msg[CONV_EXAMPLE_MSG_LEN];
    uint8_t encoded[128];   /* plenty of room for rate-1/2 order-7 */
    uint8_t decoded[CONV_EXAMPLE_MSG_LEN + 4];

    for (size_t i = 0; i < CONV_EXAMPLE_MSG_LEN; i++) {
        msg[i] = (uint8_t)(i * 19u + 3u);
    }

    correct_convolutional *conv = correct_convolutional_create(
        2, 7, correct_conv_r12_7_polynomial);
    if (!conv) return 10;

    size_t enc_bits = correct_convolutional_encode(conv, msg, CONV_EXAMPLE_MSG_LEN, encoded);
    if (enc_bits == 0) {
        correct_convolutional_destroy(conv);
        return 11;
    }

    /* Flip a handful of bits - viterbi should recover them */
    encoded[3] ^= 0x08;
    encoded[10] ^= 0x01;
    encoded[20] ^= 0x40;

    ssize_t dec_bytes = correct_convolutional_decode(conv, encoded, enc_bits, decoded);
    correct_convolutional_destroy(conv);

    if (dec_bytes < (ssize_t)CONV_EXAMPLE_MSG_LEN) return 12;
    if (memcmp(msg, decoded, CONV_EXAMPLE_MSG_LEN) != 0) return 13;

    return 0;
}

/* Returns 0 on full success. Nonzero return encodes which subtest failed. */
int fec_sterg_example_run(void) {
    int rc;

    rc = rs_example();
    if (rc) return rc;

    rc = conv_example();
    if (rc) return rc;

    return 0;
}
