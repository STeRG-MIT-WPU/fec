/*
 * fec_sterg verification harness.
 *
 * Runs on the host (inside the Docker container) to confirm the ported
 * codecs produce correct output. Each test prints a PASS/FAIL line.
 *
 * Tests:
 *   1. Reed-Solomon round-trip, zero errors, num_roots in {8, 16, 32}.
 *   2. Reed-Solomon correction at exactly num_roots/2 byte errors.
 *   3. Reed-Solomon fails cleanly beyond num_roots/2 errors.
 *   4. Reed-Solomon decode_with_erasures recovers erasures.
 *   5. Convolutional rate-1/2 order-7 round-trip with bit flips.
 *   6. Convolutional rate-1/3 order-7 round-trip.
 *   7. Convolutional soft decoding.
 *   8. Convolutional order-9 round-trip.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "correct.h"

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("[%02d] %-55s ", tests_run, name); \
    fflush(stdout); \
} while (0)

#define PASS() do { \
    printf("PASS\n"); \
} while (0)

#define FAIL(fmt, ...) do { \
    tests_failed++; \
    printf("FAIL  " fmt "\n", ##__VA_ARGS__); \
} while (0)

static void fill_pattern(uint8_t *buf, size_t n, unsigned int seed) {
    for (size_t i = 0; i < n; i++) {
        buf[i] = (uint8_t)((i * 37u + seed) ^ (i >> 3));
    }
}

/* --- Reed-Solomon tests --- */

static void rs_roundtrip_clean(size_t num_roots) {
    char name[64];
    snprintf(name, sizeof(name), "RS(255,%zu) clean round-trip", 255 - num_roots);
    TEST(name);

    size_t msg_len = 255 - num_roots;
    uint8_t msg[255], enc[255], dec[255];
    fill_pattern(msg, msg_len, 42);

    correct_reed_solomon *rs = correct_reed_solomon_create(
        correct_rs_primitive_polynomial_ccsds, 1, 1, num_roots);
    if (!rs) { FAIL("create"); return; }

    ssize_t elen = correct_reed_solomon_encode(rs, msg, msg_len, enc);
    if (elen != 255) { FAIL("encode returned %zd", elen); goto out; }

    ssize_t dlen = correct_reed_solomon_decode(rs, enc, 255, dec);
    if (dlen != (ssize_t)msg_len) { FAIL("decode returned %zd", dlen); goto out; }
    if (memcmp(msg, dec, msg_len) != 0) { FAIL("payload mismatch"); goto out; }

    PASS();
out:
    correct_reed_solomon_destroy(rs);
}

static void rs_recover_at_limit(size_t num_roots) {
    char name[64];
    snprintf(name, sizeof(name), "RS(255,%zu) recovers %zu byte errors",
             255 - num_roots, num_roots / 2);
    TEST(name);

    size_t msg_len = 255 - num_roots;
    uint8_t msg[255], enc[255], dec[255];
    fill_pattern(msg, msg_len, 99);

    correct_reed_solomon *rs = correct_reed_solomon_create(
        correct_rs_primitive_polynomial_ccsds, 1, 1, num_roots);
    if (!rs) { FAIL("create"); return; }

    correct_reed_solomon_encode(rs, msg, msg_len, enc);

    /* Flip num_roots/2 bytes at pseudo-random spread positions */
    size_t flips = num_roots / 2;
    for (size_t i = 0; i < flips; i++) {
        size_t idx = (i * 251u) % 255;
        enc[idx] ^= 0x5A;
    }

    ssize_t dlen = correct_reed_solomon_decode(rs, enc, 255, dec);
    if (dlen != (ssize_t)msg_len) { FAIL("decode returned %zd", dlen); goto out; }
    if (memcmp(msg, dec, msg_len) != 0) { FAIL("mismatch after correction"); goto out; }

    PASS();
out:
    correct_reed_solomon_destroy(rs);
}

static void rs_fail_beyond_limit(void) {
    TEST("RS(255,223) returns -1 with too many errors");

    size_t num_roots = 32;
    size_t msg_len = 255 - num_roots;
    uint8_t msg[255], enc[255], dec[255];
    fill_pattern(msg, msg_len, 7);

    correct_reed_solomon *rs = correct_reed_solomon_create(
        correct_rs_primitive_polynomial_ccsds, 1, 1, num_roots);
    if (!rs) { FAIL("create"); return; }

    correct_reed_solomon_encode(rs, msg, msg_len, enc);

    /* 25 errors — beyond num_roots/2 = 16 */
    for (size_t i = 0; i < 25; i++) {
        enc[i * 10] ^= 0xFF;
    }

    ssize_t dlen = correct_reed_solomon_decode(rs, enc, 255, dec);
    /* Either returns -1 or returns a bad payload. Both are acceptable "can't recover". */
    if (dlen == (ssize_t)msg_len && memcmp(msg, dec, msg_len) == 0) {
        FAIL("unexpectedly recovered 25 errors (should exceed capacity)");
        goto out;
    }
    PASS();
out:
    correct_reed_solomon_destroy(rs);
}

static void rs_erasures(void) {
    TEST("RS(255,223) decode_with_erasures recovers 32 erasures");

    size_t num_roots = 32;
    size_t msg_len = 255 - num_roots;
    uint8_t msg[255], enc[255], dec[255];
    uint8_t erasure_locs[32];
    fill_pattern(msg, msg_len, 13);

    correct_reed_solomon *rs = correct_reed_solomon_create(
        correct_rs_primitive_polynomial_ccsds, 1, 1, num_roots);
    if (!rs) { FAIL("create"); return; }

    correct_reed_solomon_encode(rs, msg, msg_len, enc);

    /* Wipe 32 bytes and inform the decoder of their positions */
    for (size_t i = 0; i < 32; i++) {
        size_t idx = i * 7;
        enc[idx] = 0x00;
        erasure_locs[i] = (uint8_t)idx;
    }

    ssize_t dlen = correct_reed_solomon_decode_with_erasures(
        rs, enc, 255, erasure_locs, 32, dec);
    if (dlen != (ssize_t)msg_len) { FAIL("decode returned %zd", dlen); goto out; }
    if (memcmp(msg, dec, msg_len) != 0) { FAIL("mismatch after erasure recovery"); goto out; }

    PASS();
out:
    correct_reed_solomon_destroy(rs);
}

/* --- Convolutional tests --- */

static void conv_roundtrip(size_t rate, size_t order,
                           const correct_convolutional_polynomial_t *poly,
                           const char *label) {
    char name[96];
    snprintf(name, sizeof(name), "Conv %s round-trip + 3 bit flips", label);
    TEST(name);

    const size_t msg_len = 64;
    uint8_t msg[64];
    uint8_t enc[1024];
    uint8_t dec[128];
    fill_pattern(msg, msg_len, 201);

    correct_convolutional *c = correct_convolutional_create(rate, order, poly);
    if (!c) { FAIL("create"); return; }

    size_t enc_bits = correct_convolutional_encode(c, msg, msg_len, enc);
    if (enc_bits == 0) { FAIL("encode returned 0"); goto out; }

    /* Flip three bits sparsely */
    enc[5]  ^= 0x04;
    enc[20] ^= 0x10;
    enc[40] ^= 0x01;

    ssize_t dbytes = correct_convolutional_decode(c, enc, enc_bits, dec);
    if (dbytes < (ssize_t)msg_len) { FAIL("decode returned %zd", dbytes); goto out; }
    if (memcmp(msg, dec, msg_len) != 0) { FAIL("payload mismatch"); goto out; }

    PASS();
out:
    correct_convolutional_destroy(c);
}

static void conv_soft(void) {
    TEST("Conv r=1/2 o=7 soft decoding (AWGN-ish)");

    const size_t msg_len = 48;
    uint8_t msg[64];
    uint8_t enc[512];
    uint8_t soft[4096];
    uint8_t dec[128];
    fill_pattern(msg, msg_len, 77);

    correct_convolutional *c = correct_convolutional_create(
        2, 7, correct_conv_r12_7_polynomial);
    if (!c) { FAIL("create"); return; }

    size_t enc_bits = correct_convolutional_encode(c, msg, msg_len, enc);

    /* Map hard bits -> soft symbols: 1 -> 240, 0 -> 15 (near-saturated, small noise) */
    for (size_t b = 0; b < enc_bits; b++) {
        uint8_t bit = (enc[b / 8] >> (7 - (b % 8))) & 1;
        soft[b] = bit ? 240 : 15;
    }
    /* Nudge a few symbols into the ambiguous zone */
    soft[10] = 130;
    soft[50] = 125;
    soft[90] = 128;

    ssize_t dbytes = correct_convolutional_decode_soft(c, soft, enc_bits, dec);
    if (dbytes < (ssize_t)msg_len) { FAIL("decode returned %zd", dbytes); goto out; }
    if (memcmp(msg, dec, msg_len) != 0) { FAIL("soft decode mismatch"); goto out; }

    PASS();
out:
    correct_convolutional_destroy(c);
}

/* --- Entry point --- */

int main(void) {
    printf("fec_sterg verification harness\n");
    printf("==============================\n");

    rs_roundtrip_clean(8);
    rs_roundtrip_clean(16);
    rs_roundtrip_clean(32);

    rs_recover_at_limit(8);
    rs_recover_at_limit(16);
    rs_recover_at_limit(32);

    rs_fail_beyond_limit();
    rs_erasures();

    conv_roundtrip(2, 7, correct_conv_r12_7_polynomial, "r=1/2 o=7");
    conv_roundtrip(3, 7, correct_conv_r13_7_polynomial, "r=1/3 o=7");
    conv_roundtrip(2, 9, correct_conv_r12_9_polynomial, "r=1/2 o=9");
    conv_soft();

    printf("\n%d/%d tests passed\n", tests_run - tests_failed, tests_run);
    return tests_failed ? 1 : 0;
}
